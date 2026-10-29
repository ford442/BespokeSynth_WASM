import assert from 'node:assert/strict';
import {
  CLOCK_BEAT,
  CLOCK_BPM,
  CLOCK_FRAME_END,
  CLOCK_PLAYING,
  CLOCK_SAMPLE_RATE,
  CLOCK_SEQ,
  EVENT_WORDS,
  RING_READ,
  RING_WRITE,
  SCRIPT_NOTE_ON,
  SCRIPT_NOTE_TIMED,
  createScriptChannel,
  type ScriptChannel,
} from '../../src/audio/engineShared.ts';
import {
  BLOCKED_WORKER_GLOBALS,
  MAX_SCRIPT_BYTES,
  WORKER_BOOTSTRAP,
  sanitizeWorkerMessage,
  validateScriptSource,
} from '../../src/script/scriptSandbox.ts';

assert.equal(validateScriptSource('bespoke.log("hi")'), null);
assert.match(validateScriptSource('import("https://example.com/x.js")') ?? '', /import/);
assert.match(validateScriptSource('import x from "y"') ?? '', /import/);
assert.match(validateScriptSource('x'.repeat(MAX_SCRIPT_BYTES + 1)) ?? '', /larger/);

for (const name of ['fetch', 'XMLHttpRequest', 'WebSocket', 'importScripts', 'indexedDB', 'WebAssembly', 'postMessage']) {
  assert.ok(BLOCKED_WORKER_GLOBALS.includes(name), `${name} is scrubbed from the worker`);
  assert.ok(WORKER_BOOTSTRAP.includes(`"${name}"`), `bootstrap embeds ${name}`);
}
assert.ok(!WORKER_BOOTSTRAP.includes('__BLOCKED__'));
assert.doesNotThrow(() => new Function(WORKER_BOOTSTRAP), 'bootstrap is valid JavaScript');

assert.deepEqual(sanitizeWorkerMessage({ type: 'setControl', id: 3, name: 'frequency', value: 440 }), {
  type: 'setControl', id: 3, name: 'frequency', value: 440,
});
assert.equal(sanitizeWorkerMessage({ type: 'setControl', id: 3, name: 'frequency', value: Infinity }), null);
assert.equal(sanitizeWorkerMessage({ type: 'setControl', id: 3, name: '../../evil', value: 1 }), null);
assert.equal(sanitizeWorkerMessage({ type: 'setControl', id: 1.5, name: 'gain', value: 1 }), null);
assert.equal(sanitizeWorkerMessage({ type: 'note', pitch: 200, on: true }), null);
assert.deepEqual(sanitizeWorkerMessage({ type: 'note', pitch: 60, velocity: 5, on: true }), {
  type: 'note', pitch: 60, velocity: 1, on: true,
});
assert.equal(sanitizeWorkerMessage({ type: 'eval', code: 'x' }), null);
assert.equal(sanitizeWorkerMessage(null), null);
const longLog = sanitizeWorkerMessage({ type: 'log', text: 'y'.repeat(10000) });
assert.ok(longLog && longLog.type === 'log' && longLog.text.length === 500);

// --- The bootstrap itself, run against a fake worker scope and a real ScriptChannel ---

type Listener = (event: { data: unknown }) => void;

/** One worker: its scope's postMessage, timers on a manual clock, and the message listener. */
function bootWorker() {
  const posted: Array<Record<string, unknown>> = [];
  const listeners: Listener[] = [];
  const timers = new Map<number, { fn: () => void; at: number }>();
  let now = 0;
  let nextTimer = 1;
  const scope = {
    postMessage: (message: Record<string, unknown>) => posted.push(message),
    addEventListener: (type: string, fn: Listener) => {
      if (type === 'message') listeners.push(fn);
    },
    setTimeout: (fn: () => void, ms: number) => {
      const id = nextTimer++;
      timers.set(id, { fn, at: now + Math.max(0, ms) });
      return id;
    },
    clearTimeout: (id: number) => timers.delete(id),
  };
  new Function('self', WORKER_BOOTSTRAP)(scope);
  return {
    posted,
    send: (data: unknown) => listeners.forEach((listener) => listener({ data })),
    /** Runs every timer due within `ms`, in time order. */
    advance(ms: number) {
      const end = now + ms;
      for (;;) {
        let next: [number, { fn: () => void; at: number }] | null = null;
        for (const entry of timers) if (entry[1].at <= end && (!next || entry[1].at < next[1].at)) next = entry;
        if (!next) break;
        timers.delete(next[0]);
        now = next[1].at;
        next[1].fn();
      }
      now = end;
    },
  };
}

const RATE = 48000;
const BPM = 120;
const FRAMES_PER_BEAT = (RATE * 60) / BPM;
const BEAT0 = 100_000; // the AudioContext frame the transport's beat 0 played on

/** What the engine processor does after each quantum (seqlock write). */
function publishClock(channel: ScriptChannel, frameEnd: number, playing = true): void {
  const { ints, doubles } = channel.clock;
  const seq = Atomics.load(ints, CLOCK_SEQ) | 1;
  Atomics.store(ints, CLOCK_SEQ, seq);
  doubles[CLOCK_BEAT] = playing ? (frameEnd - BEAT0) / FRAMES_PER_BEAT : 0;
  doubles[CLOCK_BPM] = BPM;
  doubles[CLOCK_FRAME_END] = frameEnd;
  doubles[CLOCK_SAMPLE_RATE] = RATE;
  Atomics.store(ints, CLOCK_PLAYING, playing ? 1 : 0);
  Atomics.store(ints, CLOCK_SEQ, seq + 1);
}

/** Records the worker wrote since the last call: [pitch, on, timed, velocity, frame]. */
function takeRecords(channel: ScriptChannel): Array<[number, boolean, boolean, number, number]> {
  const { header, words } = channel.ring;
  const capacity = Atomics.load(header, 2);
  let read = Atomics.load(header, RING_READ);
  const write = Atomics.load(header, RING_WRITE);
  const out: Array<[number, boolean, boolean, number, number]> = [];
  const bits = new Int32Array(1);
  const asFloat = new Float32Array(bits.buffer);
  while (read !== write) {
    bits[0] = words[read + 1];
    const flags = words[read];
    out.push([flags & 0x7f, (flags & SCRIPT_NOTE_ON) !== 0, (flags & SCRIPT_NOTE_TIMED) !== 0, asFloat[0], words[read + 2]]);
    read = read + EVENT_WORDS >= capacity ? 0 : read + EVENT_WORDS;
  }
  Atomics.store(header, RING_READ, read);
  return out;
}

const transfer = (channel: ScriptChannel) => ({ ring: channel.ring.sab, clock: channel.clock.sab });
const run = (code: string, channel: ScriptChannel | null) => ({
  type: 'run',
  code,
  modules: [],
  scriptModuleId: 7,
  channel: channel ? transfer(channel) : null,
});

// Beats fire ahead of the engine and their notes carry the beat's frame; durations become
// timed note-offs written once inside the lookahead. User code replacing Atomics changes nothing.
{
  const channel = createScriptChannel();
  const worker = bootWorker();
  // Here the "worker" shares Node's global with the test, so put Atomics back after the script ran.
  const { load, store } = Atomics;
  worker.send(
    run(
      `Atomics.store = () => {}; Atomics.load = () => 0;
       bespoke.noteOn(40, 1);
       bespoke.onBeat((beat) => bespoke.noteOn(60 + beat, 0.5, 250));`,
      channel,
    ),
  );
  Object.assign(Atomics, { load, store });
  assert.deepEqual(takeRecords(channel), [[40, true, false, 1, 0]], 'a note outside a beat plays at once');
  assert.ok(!worker.posted.some((m) => m.type === 'note'), 'direct notes never go through the host');

  publishClock(channel, BEAT0 + 128);
  worker.advance(5);
  // Beat 0 is already rendered when the worker hears of Play: it plays at once, not late.
  assert.deepEqual(takeRecords(channel), [[60, true, false, 0.5, 0]]);
  publishClock(channel, BEAT0 + 12_128 - 1000);
  worker.advance(5);
  assert.deepEqual(takeRecords(channel), [[60, false, true, 0, BEAT0 + 12_000]], 'durations count from the beat');
  publishClock(channel, BEAT0 + FRAMES_PER_BEAT - 2000);
  worker.advance(5);
  assert.deepEqual(takeRecords(channel), [], 'beat 1 waits until it is inside the lookahead');
  publishClock(channel, BEAT0 + FRAMES_PER_BEAT - 1000);
  worker.advance(5);
  assert.deepEqual(takeRecords(channel), [[61, true, true, 0.5, BEAT0 + FRAMES_PER_BEAT]], 'beat 1 on its own frame');

  // A stall of several seconds skips the missed beats instead of bursting them.
  publishClock(channel, BEAT0 + 10 * FRAMES_PER_BEAT + 500);
  worker.advance(5);
  const afterStall = takeRecords(channel);
  assert.ok(
    afterStall.filter((r) => r[1]).length <= 1,
    `no burst after a stall (${afterStall.length} records)`,
  );

  // Back to the host's messages once the engine goes away.
  worker.send({ type: 'detach' });
  worker.posted.length = 0;
  worker.send({ type: 'tick', beat: 0.5, bpm: 120, playing: true });
  assert.ok(worker.posted.some((m) => m.type === 'note' && m.pitch === 60), 'relay mode after detach');
  assert.deepEqual(takeRecords(channel), []);
}

// A one-beat note ends on the frame the next beat's note on the same key starts: off goes first.
{
  const channel = createScriptChannel();
  const worker = bootWorker();
  worker.send(run(`bespoke.onBeat(() => bespoke.noteOn(72, 1, 500));`, channel));
  publishClock(channel, BEAT0 + 64);
  worker.advance(5);
  publishClock(channel, BEAT0 + FRAMES_PER_BEAT - 500);
  worker.advance(5);
  publishClock(channel, BEAT0 + 2 * FRAMES_PER_BEAT - 500);
  worker.advance(5);
  const records = takeRecords(channel).filter((r) => r[2]);
  assert.deepEqual(
    records.map((r) => [r[1], r[4] - BEAT0]),
    [
      [false, FRAMES_PER_BEAT],
      [true, FRAMES_PER_BEAT],
      [false, 2 * FRAMES_PER_BEAT],
      [true, 2 * FRAMES_PER_BEAT],
    ],
    'frame order, a note-off before a note-on at the same frame',
  );
}

// Controls set in a beat that fired ahead are sent when the beat is about to play.
{
  const channel = createScriptChannel();
  const worker = bootWorker();
  worker.send(run(`bespoke.onBeat((beat) => bespoke.setControl(3, 'gain', beat));`, channel));
  publishClock(channel, BEAT0 + 64);
  worker.advance(5);
  worker.posted.length = 0;
  publishClock(channel, BEAT0 + FRAMES_PER_BEAT - 960); // 20 ms before beat 1
  worker.advance(5);
  assert.ok(!worker.posted.some((m) => m.type === 'setControl'), 'not sent 20 ms early');
  worker.advance(25);
  assert.deepEqual(
    worker.posted.filter((m) => m.type === 'setControl'),
    [{ type: 'setControl', id: 3, name: 'gain', value: 1 }],
  );
}

// Out-of-range pitches never reach the ring; a stopped transport fires no beats.
{
  const channel = createScriptChannel();
  const worker = bootWorker();
  worker.send(run(`bespoke.noteOn(200); bespoke.noteOn(-1); bespoke.onBeat(() => bespoke.noteOn(50));`, channel));
  publishClock(channel, BEAT0 + 5 * FRAMES_PER_BEAT, false);
  worker.advance(50);
  assert.deepEqual(takeRecords(channel), []);
}

console.log('scriptSandbox tests passed');
