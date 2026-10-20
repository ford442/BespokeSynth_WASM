import assert from 'node:assert/strict';
import {
  CLOCK_BEAT,
  CLOCK_BPM,
  CLOCK_CLOSED,
  CLOCK_FRAME_END,
  CLOCK_SAMPLE_RATE,
  CLOCK_SEQ,
  EVENT_NOTE,
  EVENT_RING_WORDS,
  EVENT_SCRIPT_DIRECT_NOTE,
  EVENT_SCRIPT_CLEAR,
  EVENT_WORDS,
  INPUT_RING_WORDS,
  MIDI_OUT_RING_WORDS,
  RING_READ,
  RING_WRITE,
  SCRIPT_NOTE_ON,
  SCRIPT_NOTE_TIMED,
  SLOT_BEAT,
  SLOT_BPM,
  SLOT_CALLBACK_COUNT,
  SLOT_PLAYING,
  SLOT_SNAPSHOT_COUNT,
  STATUS_DOUBLES,
  STATUS_SLOT_COUNT,
  ST_INPUT_ACTIVE,
  ST_MIDI_CAPTURE,
  ST_PEAK_L,
  ST_PEAK_R,
  ST_READY,
  ST_SEQ,
  createAnalysisRing,
  createSabRing,
  createScriptChannel,
  createStatusBlock,
  floatBits,
  readAnalysisRing,
  readScriptClock,
  ringDrainWords,
  ringPushRecord,
  takePeak,
} from '../../src/audio/engineShared.ts';

// wasm/audio_worklet/engineProcessor.js is appended to the engine's Emscripten glue in the
// build; here it runs alone against a JS stand-in for createBespokeAudioEngine.

/**
 * Exposes only what BespokeSynthWASM_audio exports at runtime (HEAP8, HEAPU8, HEAPF32,
 * UTF8ToString). Growth swaps in a new ArrayBuffer, as -sGROWABLE_ARRAYBUFFERS=0 does.
 */
class FakeEngine {
  HEAP8 = new Int8Array(0);
  HEAPU8 = new Uint8Array(0);
  HEAPF32 = new Float32Array(0);
  private top = 1024;
  private errorPtr = 0;
  blocks = 0;
  initRate = 0;
  inputActive = 0;
  midiCapture = 0;
  snapshots: Array<{ length: number; first: number; reset: number }> = [];
  samples: Array<{ id: number; channels: number; frames: number; rate: number; pcm: number[]; generation: number }> =
    [];
  evicted: number[] = [];
  events: number[][] = [];
  input: number[] = [];
  pendingMidi: number[] = [];
  blockFrames: number[] = [];
  statusCount = 0;

  constructor() {
    this.grow(1 << 16);
  }

  private grow(bytes: number): void {
    const next = new ArrayBuffer(bytes);
    new Uint8Array(next).set(this.HEAPU8);
    this.HEAP8 = new Int8Array(next);
    this.HEAPU8 = new Uint8Array(next);
    this.HEAPF32 = new Float32Array(next);
  }

  _malloc(bytes: number): number {
    const ptr = (this.top + 15) & ~15;
    this.top = ptr + bytes;
    if (this.top > this.HEAPU8.length) this.grow(this.top * 2);
    return ptr;
  }

  _free(): void {}

  UTF8ToString(ptr: number): string {
    let end = ptr;
    while (this.HEAPU8[end]) end += 1;
    return new TextDecoder().decode(this.HEAPU8.slice(ptr, end));
  }

  _bespoke_audio_wire_abi(): number {
    return 77;
  }

  _bespoke_audio_init(rate: number): number {
    this.initRate = rate;
    return 0;
  }

  _bespoke_audio_apply_snapshot(ptr: number, length: number, reset: number): number {
    const first = this.HEAPU8[ptr];
    if (first === 0xee) return -1;
    this.snapshots.push({ length, first, reset });
    return 0;
  }

  _bespoke_audio_last_error(): number {
    if (!this.errorPtr) {
      const text = new TextEncoder().encode('bad snapshot\0');
      this.errorPtr = this._malloc(text.length);
      this.HEAPU8.set(text, this.errorPtr);
    }
    return this.errorPtr;
  }

  _bespoke_audio_adopt_sample(
    id: number,
    channels: number,
    frames: number,
    rate: number,
    ptr: number,
    generation: number,
  ): number {
    const pcm = Array.from(new Float32Array(this.HEAPU8.buffer, ptr, channels * frames));
    this.samples.push({ id, channels, frames, rate, pcm, generation });
    return 0;
  }

  _bespoke_audio_evict_sample(id: number): void {
    this.evicted.push(id);
  }

  _bespoke_audio_apply_events(ptr: number, count: number): void {
    const words = new Int32Array(this.HEAPU8.buffer, ptr, count * EVENT_WORDS);
    for (let i = 0; i < count; i += 1) this.events.push(Array.from(words.subarray(i * 4, i * 4 + 4)));
  }

  _bespoke_audio_push_input(ptr: number, count: number): void {
    this.input.push(...new Float32Array(this.HEAPU8.buffer, ptr, count));
  }

  _bespoke_audio_set_input_active(active: number): void {
    this.inputActive = active;
  }

  _bespoke_audio_set_midi_out_capture(enabled: number): void {
    this.midiCapture = enabled;
  }

  /** L = block number, R = -block number: a stale heap view shows up as an old block. */
  _bespoke_audio_process(left: number, right: number, frames: number, blockFrame: number): void {
    this.blocks += 1;
    this.blockFrames.push(blockFrame);
    new Float32Array(this.HEAPU8.buffer, left, frames).fill(this.blocks);
    new Float32Array(this.HEAPU8.buffer, right, frames).fill(-this.blocks);
  }

  _bespoke_audio_drain_midi_out(ptr: number, max: number): number {
    const batch = this.pendingMidi.splice(0, max);
    new Int32Array(this.HEAPU8.buffer, ptr, batch.length).set(batch);
    return batch.length;
  }

  _bespoke_audio_read_status(ptr: number, count: number): void {
    const status = new Float64Array(this.HEAPU8.buffer, ptr, count);
    status.fill(0);
    this.statusCount = count;
    status[SLOT_BEAT] = this.blocks / 4;
    status[1] = this.blocks;
    status[10] = 0.5; // peak L
    status[11] = 0.25; // peak R
    status[13] = this.snapshots.length;
    status[SLOT_BPM] = 128;
    status[SLOT_PLAYING] = 1;
    status[count - 1] = 77; // the last histogram bucket
  }
}

interface PortStub {
  posted: Array<{ type: string; message?: string; abi?: number }>;
  onmessage: ((event: { data: unknown }) => void) | null;
  postMessage(message: unknown): void;
}
type Processor = { port: PortStub; process(inputs: Float32Array[][], outputs: Float32Array[][]): boolean };
type ProcessorCtor = new (options: { processorOptions: Record<string, unknown> }) => Processor;

const registered = new Map<string, ProcessorCtor>();
const g = globalThis as Record<string, unknown>;
g.sampleRate = 48000;
g.currentFrame = 0;
g.AudioWorkletProcessor = class {
  port: PortStub = {
    posted: [],
    onmessage: null,
    postMessage(message) {
      this.posted.push(message as PortStub['posted'][number]);
    },
  };
};
g.registerProcessor = (name: string, ctor: ProcessorCtor) => registered.set(name, ctor);
let fake = new FakeEngine();
g.createBespokeAudioEngine = (options: { instantiateWasm?: unknown }) => {
  assert.equal(typeof options.instantiateWasm, 'function', 'the worklet supplies its own instantiateWasm (no fetch)');
  return Promise.resolve(fake);
};

await import('../../wasm/audio_worklet/engineProcessor.js');
const Engine = registered.get('bespoke-audio-engine');
assert.ok(Engine, 'registers bespoke-audio-engine');

const tick = () => new Promise((resolve) => setTimeout(resolve, 0));
const QUANTUM = 128;
const quantum = (): Float32Array[][] => [[new Float32Array(QUANTUM).fill(9), new Float32Array(QUANTUM).fill(9)]];
/** Runs one quantum the way the AudioWorklet does: currentFrame, then process(), then the clock moves on. */
const render = (target: Processor, outputs: Float32Array[][] = quantum()) => {
  const result = target.process([], outputs);
  g.currentFrame = (g.currentFrame as number) + QUANTUM;
  return result;
};

function makeShared() {
  return {
    events: createSabRing(EVENT_RING_WORDS),
    input: createSabRing(INPUT_RING_WORDS),
    midiOut: createSabRing(MIDI_OUT_RING_WORDS),
    analysis: createAnalysisRing(),
    status: createStatusBlock(),
  };
}

function makeProcessor(shared: ReturnType<typeof makeShared>, wireAbi = 77): Processor {
  return new Engine!({
    processorOptions: {
      wasmModule: {},
      wireAbi,
      events: shared.events.sab,
      input: shared.input.sab,
      midiOut: shared.midiOut.sab,
      analysis: shared.analysis.sab,
      status: shared.status.sab,
    },
  });
}

// Startup: messages that arrive before the engine is ready are replayed, output stays silent meanwhile.
const shared = makeShared();
const proc = makeProcessor(shared);
proc.port.onmessage?.({
  data: { type: 'sample', id: 3, generation: 9, channels: 1, frames: 4, rate: 44100, pcm: new Float32Array([1, 2, 3, 4]) },
});
proc.port.onmessage?.({ data: { type: 'snapshot', bytes: new Uint8Array([5, 6, 7]).buffer, reset: true } });
let out = quantum()[0];
assert.equal(render(proc, [out]), true);
assert.ok(out[0].every((v) => v === 0) && out[1].every((v) => v === 0), 'silent until ready');
await tick();
assert.equal(Atomics.load(shared.status.ints, ST_READY), 1, 'ready flag');
assert.deepEqual(proc.port.posted.at(-1), { type: 'ready', abi: 77 });
assert.equal(fake.initRate, 48000, 'engine initialized at the context rate');
assert.deepEqual(
  fake.samples,
  [{ id: 3, channels: 1, frames: 4, rate: 44100, pcm: [1, 2, 3, 4], generation: 9 }],
  'queued sample replayed with its generation',
);
assert.deepEqual(fake.snapshots, [{ length: 3, first: 5, reset: 1 }], 'queued snapshot replayed after the sample');
proc.port.onmessage?.({ data: { type: 'evictSample', id: 3 } });
assert.deepEqual(fake.evicted, [3], 'an eviction from the UI module drops the engine copy');

// One quantum: events and mic samples in, stereo out, scope samples and status published.
assert.ok(ringPushRecord(shared.events, EVENT_NOTE, 60 | (1 << 12), floatBits(1), 0));
const inputHeader = shared.input.header;
for (let i = 0; i < 5; i += 1) shared.input.floats[i] = i / 10;
Atomics.store(inputHeader, RING_WRITE, 5);
Atomics.store(shared.status.ints, ST_INPUT_ACTIVE, 1);
out = quantum()[0];
render(proc, [out]);
assert.deepEqual(fake.events, [[EVENT_NOTE, 60 | (1 << 12), floatBits(1), 0]], 'note record reaches the engine');
assert.equal(Atomics.load(shared.events.header, RING_READ), 4, 'event ring consumed');
assert.deepEqual(
  fake.input.map((v) => Math.round(v * 10)),
  [0, 1, 2, 3, 4],
  'mic samples reach the engine',
);
assert.equal(fake.inputActive, 1, 'input-active flag follows the status block');
assert.ok(out[0].every((v) => v === 1) && out[1].every((v) => v === -1), 'engine output copied to both channels');
const scope = new Float32Array(4096);
let { total, count } = readAnalysisRing(shared.analysis, 0, scope);
assert.equal(count, QUANTUM, 'one quantum of scope samples');
assert.ok(
  scope.subarray(0, count).every((v) => v === 0),
  'scope carries the mid (L+R)/2',
);
assert.equal(shared.status.doubles[SLOT_CALLBACK_COUNT], 1);
assert.equal(shared.status.doubles[SLOT_SNAPSHOT_COUNT], 1);
assert.equal(takePeak(shared.status, ST_PEAK_L), 0.5);
assert.equal(takePeak(shared.status, ST_PEAK_R), 0.25);
assert.equal(takePeak(shared.status, ST_PEAK_L), 0, 'peaks are taken');
assert.equal(Atomics.load(shared.status.ints, ST_SEQ), 1);
assert.deepEqual(fake.blockFrames, [QUANTUM], 'blocks render on the AudioContext frame clock (currentFrame)');
assert.ok(STATUS_DOUBLES >= STATUS_SLOT_COUNT, 'the status block holds every engine slot');
assert.equal(fake.statusCount, STATUS_SLOT_COUNT);
assert.equal(shared.status.doubles[STATUS_SLOT_COUNT - 1], 77, 'the whole block-time histogram is published');

// MIDI out only drains while the host asks for it.
fake.pendingMidi = [11, 12, 13];
render(proc, [quantum()[0]]);
const midi = new Int32Array(64);
assert.equal(ringDrainWords(shared.midiOut, midi), 0, 'no capture: notes stay in the engine');
Atomics.store(shared.status.ints, ST_MIDI_CAPTURE, 1);
render(proc, [quantum()[0]]);
assert.equal(fake.midiCapture, 1);
assert.deepEqual(Array.from(midi.subarray(0, ringDrainWords(shared.midiOut, midi))), [11, 12, 13]);

// A big snapshot grows the heap; the next quantum must render through fresh views.
const big = new Uint8Array(200_000).fill(1);
proc.port.onmessage?.({ data: { type: 'snapshot', bytes: big.buffer, reset: false } });
assert.equal(fake.snapshots.at(-1)?.length, 200_000);
out = quantum()[0];
render(proc, [out]);
assert.ok(
  out[0].every((v) => v === fake.blocks),
  'output follows heap growth',
);
({ total, count } = readAnalysisRing(shared.analysis, total, scope));
assert.equal(count, QUANTUM * 3);

// A rejected snapshot is reported with the engine's reason and keeps the engine running.
proc.port.onmessage?.({ data: { type: 'snapshot', bytes: new Uint8Array([0xee]).buffer, reset: false } });
assert.deepEqual(proc.port.posted.at(-1), { type: 'error', message: 'bad snapshot' });
assert.equal(render(proc, [quantum()[0]]), true);

// Mono destination: only the left channel exists.
const mono = [new Float32Array(QUANTUM)];
render(proc, [mono]);
assert.ok(
  mono[0].every((v) => v === fake.blocks),
  'mono output gets the left channel',
);

// Script channels: a worker's records go straight to the engine, stamped with the registered module.
{
  const channel = createScriptChannel();
  const events = fake.events.length;
  proc.port.onmessage?.({ data: { type: 'scriptChannel', moduleId: 42, ring: channel.ring.sab, clock: channel.clock.sab } });
  assert.equal(readScriptClock(channel.clock), null, 'no clock before the first quantum');
  const frame = (g.currentFrame as number) + 300;
  // The worker's own record format; word 3 is ignored (it cannot pick the module).
  assert.ok(ringPushRecord(channel.ring, 64 | SCRIPT_NOTE_ON | SCRIPT_NOTE_TIMED, floatBits(0.5), frame, 999));
  assert.ok(ringPushRecord(channel.ring, 64, 0, 0, 0));
  // ScriptClear on the main ring in the same quantum lands after the script's own notes.
  assert.ok(ringPushRecord(shared.events, EVENT_SCRIPT_CLEAR, 42, 0, 0));
  const start = g.currentFrame as number;
  render(proc);
  assert.deepEqual(
    fake.events.slice(events),
    [
      [EVENT_SCRIPT_DIRECT_NOTE, 43 | (64 << 20) | (1 << 27) | (1 << 28), floatBits(0.5), frame],
      [EVENT_SCRIPT_DIRECT_NOTE, 43 | (64 << 20), 0, 0],
      [EVENT_SCRIPT_CLEAR, 42, 0, 0],
    ],
    'script records become ScriptDirectNote events ahead of the main ring',
  );
  assert.equal(Atomics.load(channel.ring.header, RING_READ), 8, 'script ring consumed');
  const clock = readScriptClock(channel.clock);
  assert.deepEqual(
    clock,
    { beat: fake.blocks / 4, bpm: 128, playing: true, frameEnd: start + QUANTUM, sampleRate: 48000 },
    'the clock carries the transport at the next quantum\'s first frame',
  );
  assert.equal(Atomics.load(channel.clock.ints, CLOCK_SEQ) % 2, 0, 'seqlock is even after a write');
  assert.equal(channel.clock.doubles[CLOCK_FRAME_END], start + QUANTUM);
  assert.ok(channel.clock.doubles[CLOCK_BEAT] > 0 && channel.clock.doubles[CLOCK_BPM] === 128);
  assert.equal(channel.clock.doubles[CLOCK_SAMPLE_RATE], 48000);

  // A header no ring can have (written by user code) is reset, never followed.
  Atomics.store(channel.ring.header, RING_WRITE, 5);
  render(proc);
  assert.equal(fake.events.length, events + 3, 'a corrupt header delivers nothing');
  assert.equal(Atomics.load(channel.ring.header, RING_WRITE), 0);
  assert.equal(Atomics.load(channel.ring.header, RING_READ), 0);
  // A huge write index is out of range for the processor's own capacity, whatever the header claims.
  Atomics.store(channel.ring.header, 2, 1 << 30);
  Atomics.store(channel.ring.header, RING_WRITE, 1 << 20);
  render(proc);
  assert.equal(fake.events.length, events + 3);

  // At most one quantum's share per script: a flood waits for later quanta.
  Atomics.store(channel.ring.header, 2, 256 * EVENT_WORDS);
  Atomics.store(channel.ring.header, RING_WRITE, 0);
  Atomics.store(channel.ring.header, RING_READ, 0);
  for (let i = 0; i < 100; i += 1) ringPushRecord(channel.ring, 60 | SCRIPT_NOTE_ON, floatBits(1), 0, 0);
  const before = fake.events.length;
  render(proc);
  assert.equal(fake.events.length - before, 32, 'per-quantum cap per script');
  render(proc);
  render(proc);
  render(proc);
  assert.equal(fake.events.length - before, 100, 'the rest follow');

  // Re-registering the same module replaces its channel; closing drops it.
  const replacement = createScriptChannel();
  proc.port.onmessage?.({
    data: { type: 'scriptChannel', moduleId: 42, ring: replacement.ring.sab, clock: replacement.clock.sab },
  });
  ringPushRecord(channel.ring, 61 | SCRIPT_NOTE_ON, 0, 0, 0);
  ringPushRecord(replacement.ring, 62 | SCRIPT_NOTE_ON, 0, 0, 0);
  const atReplace = fake.events.length;
  render(proc);
  assert.deepEqual(fake.events.slice(atReplace), [[EVENT_SCRIPT_DIRECT_NOTE, 43 | (62 << 20) | (1 << 27), 0, 0]]);
  Atomics.store(replacement.clock.ints, CLOCK_CLOSED, 1);
  ringPushRecord(replacement.ring, 63 | SCRIPT_NOTE_ON, 0, 0, 0);
  const seqAtClose = Atomics.load(replacement.clock.ints, CLOCK_SEQ);
  render(proc);
  assert.equal(fake.events.length, atReplace + 1, 'a closed channel is dropped unread');
  assert.equal(Atomics.load(replacement.clock.ints, CLOCK_SEQ), seqAtClose, 'and its clock stops');

  // Bad registrations are ignored.
  for (const bad of [
    { moduleId: -1, ring: replacement.ring.sab, clock: replacement.clock.sab },
    { moduleId: 1.5, ring: replacement.ring.sab, clock: replacement.clock.sab },
    { moduleId: 7, ring: new ArrayBuffer(64), clock: replacement.clock.sab },
    { moduleId: 7, ring: replacement.ring.sab, clock: new SharedArrayBuffer(8) },
    { moduleId: 7, ring: new SharedArrayBuffer(20), clock: replacement.clock.sab },
  ]) {
    proc.port.onmessage?.({ data: { type: 'scriptChannel', ...bad } });
  }
  ringPushRecord(replacement.ring, 63 | SCRIPT_NOTE_ON, 0, 0, 0);
  render(proc);
  assert.equal(fake.events.length, atReplace + 1, 'invalid channels never register');
}

// Wrong pairing: the UI and audio modules come from different builds.
fake = new FakeEngine();
const mismatched = makeShared();
const stale = makeProcessor(mismatched, 76);
await tick();
assert.equal(Atomics.load(mismatched.status.ints, ST_READY), -1, 'ABI mismatch fails the engine');
assert.match(stale.port.posted.at(-1)?.message ?? '', /ABI mismatch/);
out = quantum()[0];
stale.process([], [out]);
assert.ok(
  out[0].every((v) => v === 0),
  'a failed engine stays silent',
);

console.log('engineProcessor.test.ts passed');
