import assert from 'node:assert/strict';
import {
  EVENT_NOTE,
  EVENT_RING_WORDS,
  EVENT_WORDS,
  INPUT_RING_WORDS,
  MIDI_OUT_RING_WORDS,
  RING_READ,
  RING_WRITE,
  SLOT_CALLBACK_COUNT,
  SLOT_SNAPSHOT_COUNT,
  ST_INPUT_ACTIVE,
  ST_MIDI_CAPTURE,
  ST_PEAK_L,
  ST_PEAK_R,
  ST_READY,
  ST_SEQ,
  createAnalysisRing,
  createSabRing,
  createStatusBlock,
  floatBits,
  readAnalysisRing,
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
  HEAP8!: Int8Array;
  HEAPU8!: Uint8Array;
  HEAPF32!: Float32Array;
  private top = 1024;
  private errorPtr = 0;
  blocks = 0;
  initRate = 0;
  inputActive = 0;
  midiCapture = 0;
  snapshots: Array<{ length: number; first: number; reset: number }> = [];
  samples: Array<{ id: number; channels: number; frames: number; rate: number; pcm: number[] }> = [];
  events: number[][] = [];
  input: number[] = [];
  pendingMidi: number[] = [];

  constructor() {
    this.grow(1 << 16);
  }

  private grow(bytes: number): void {
    const next = new ArrayBuffer(bytes);
    if (this.HEAPU8) new Uint8Array(next).set(this.HEAPU8);
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

  _bespoke_audio_adopt_sample(id: number, channels: number, frames: number, rate: number, ptr: number): number {
    const pcm = Array.from(new Float32Array(this.HEAPU8.buffer, ptr, channels * frames));
    this.samples.push({ id, channels, frames, rate, pcm });
    return 0;
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
  _bespoke_audio_process(left: number, right: number, frames: number): void {
    this.blocks += 1;
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
    status[1] = this.blocks;
    status[10] = 0.5; // peak L
    status[11] = 0.25; // peak R
    status[13] = this.snapshots.length;
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
  data: { type: 'sample', id: 3, channels: 1, frames: 4, rate: 44100, pcm: new Float32Array([1, 2, 3, 4]) },
});
proc.port.onmessage?.({ data: { type: 'snapshot', bytes: new Uint8Array([5, 6, 7]).buffer, reset: true } });
let out = quantum()[0];
assert.equal(proc.process([], [out]), true);
assert.ok(out[0].every((v) => v === 0) && out[1].every((v) => v === 0), 'silent until ready');
await tick();
assert.equal(Atomics.load(shared.status.ints, ST_READY), 1, 'ready flag');
assert.deepEqual(proc.port.posted.at(-1), { type: 'ready', abi: 77 });
assert.equal(fake.initRate, 48000, 'engine initialized at the context rate');
assert.deepEqual(
  fake.samples,
  [{ id: 3, channels: 1, frames: 4, rate: 44100, pcm: [1, 2, 3, 4] }],
  'queued sample replayed',
);
assert.deepEqual(fake.snapshots, [{ length: 3, first: 5, reset: 1 }], 'queued snapshot replayed after the sample');

// One quantum: events and mic samples in, stereo out, scope samples and status published.
assert.ok(ringPushRecord(shared.events, EVENT_NOTE, 60 | (1 << 12), floatBits(1), 0));
const inputHeader = shared.input.header;
for (let i = 0; i < 5; i += 1) shared.input.floats[i] = i / 10;
Atomics.store(inputHeader, RING_WRITE, 5);
Atomics.store(shared.status.ints, ST_INPUT_ACTIVE, 1);
out = quantum()[0];
proc.process([], [out]);
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

// MIDI out only drains while the host asks for it.
fake.pendingMidi = [11, 12, 13];
proc.process([], [quantum()[0]]);
const midi = new Int32Array(64);
assert.equal(ringDrainWords(shared.midiOut, midi), 0, 'no capture: notes stay in the engine');
Atomics.store(shared.status.ints, ST_MIDI_CAPTURE, 1);
proc.process([], [quantum()[0]]);
assert.equal(fake.midiCapture, 1);
assert.deepEqual(Array.from(midi.subarray(0, ringDrainWords(shared.midiOut, midi))), [11, 12, 13]);

// A big snapshot grows the heap; the next quantum must render through fresh views.
const big = new Uint8Array(200_000).fill(1);
proc.port.onmessage?.({ data: { type: 'snapshot', bytes: big.buffer, reset: false } });
assert.equal(fake.snapshots.at(-1)?.length, 200_000);
out = quantum()[0];
proc.process([], [out]);
assert.ok(
  out[0].every((v) => v === fake.blocks),
  'output follows heap growth',
);
({ total, count } = readAnalysisRing(shared.analysis, total, scope));
assert.equal(count, QUANTUM * 3);

// A rejected snapshot is reported with the engine's reason and keeps the engine running.
proc.port.onmessage?.({ data: { type: 'snapshot', bytes: new Uint8Array([0xee]).buffer, reset: false } });
assert.deepEqual(proc.port.posted.at(-1), { type: 'error', message: 'bad snapshot' });
assert.equal(proc.process([], [quantum()[0]]), true);

// Mono destination: only the left channel exists.
const mono = [new Float32Array(QUANTUM)];
proc.process([], [mono]);
assert.ok(
  mono[0].every((v) => v === fake.blocks),
  'mono output gets the left channel',
);

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
