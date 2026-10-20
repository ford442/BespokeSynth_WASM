import assert from 'node:assert/strict';
import { GRAPH_NOTE_BATCH, GraphNotePump, graphNoteBytes, unpackGraphNote } from '../../src/app/graphMidiOut.ts';
import { PerformanceEcho } from '../../src/app/performanceEcho.ts';
import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth.d.ts';

const pack = (pitch: number, velocity: number, channel: number, on: boolean) =>
  pitch | (velocity << 8) | (channel << 16) | ((on ? 1 : 0) << 20);

assert.deepEqual(unpackGraphNote(pack(60, 100, 9, true)), {
  pitch: 60,
  velocity: 100,
  channel: 9,
  on: true,
});
assert.deepEqual(unpackGraphNote(pack(127, 0, 15, false)), {
  pitch: 127,
  velocity: 0,
  channel: 15,
  on: false,
});
assert.deepEqual(graphNoteBytes({ pitch: 60, velocity: 100, channel: 9, on: true }), [0x99, 60, 100]);
assert.deepEqual(
  graphNoteBytes({ pitch: 60, velocity: 0, channel: 0, on: true }),
  [0x90, 60, 1],
  'a note-on never carries velocity 0',
);
assert.deepEqual(graphNoteBytes({ pitch: 60, velocity: 77, channel: 2, on: false }), [0x82, 60, 0]);

/** A stand-in for the WASM module: a queue the "engine" fills and the drain call pops. */
function fakeModule() {
  const queue: number[] = [];
  const buffer = new ArrayBuffer(GRAPH_NOTE_BATCH * 8);
  const heap = new Int32Array(buffer);
  const calls = { capture: [] as number[], drains: 0, freed: [] as number[] };
  // Like the real build, only HEAPU8 is exported (no HEAP32), so the pump has to view its buffer itself.
  const module = {
    HEAPU8: new Uint8Array(buffer),
    _malloc: () => 16, // byte offset 16 -> heap index 4
    _free: (ptr: number) => calls.freed.push(ptr),
    _bespoke_set_graph_note_capture: (on: number) => calls.capture.push(on),
    _bespoke_drain_graph_notes: (ptr: number, max: number) => {
      calls.drains += 1;
      const got = Math.min(max, queue.length);
      for (let i = 0; i < got; i += 1) heap[(ptr >> 2) + i] = queue.shift() as number;
      return got;
    },
  } as unknown as BespokeSynthModule;
  return { module, queue, calls };
}

function fakeSink(initial: boolean) {
  const sent: Array<{ bytes: number[]; timestamp?: number }> = [];
  return {
    hasOutputValue: initial,
    sent,
    hasOutput() {
      return this.hasOutputValue;
    },
    send(bytes: number[], timestamp?: number) {
      sent.push({ bytes, timestamp });
    },
  };
}

{
  const { module, queue, calls } = fakeModule();
  const sink = fakeSink(false);
  const pump = new GraphNotePump(() => module, sink, {
    now: () => 1000,
    latencyMs: () => 40,
  });

  queue.push(pack(60, 90, 0, true));
  assert.equal(pump.tick(), 0, 'nothing is sent while no MIDI output is selected');
  assert.equal(calls.drains, 0, 'and the queue is left alone');
  assert.deepEqual(calls.capture, [], 'capture was never requested');

  sink.hasOutputValue = true;
  assert.equal(pump.tick(), 1, 'selecting an output starts forwarding');
  assert.deepEqual(calls.capture, [1]);
  assert.deepEqual(sink.sent, [{ bytes: [0x90, 60, 90], timestamp: 1040 }], 'timestamp = now + queued audio');

  queue.push(pack(60, 0, 0, false), pack(64, 100, 1, true));
  pump.tick();
  assert.deepEqual(
    sink.sent.slice(1).map((m) => m.bytes),
    [
      [0x80, 60, 0],
      [0x91, 64, 100],
    ],
    'order and channels survive',
  );
  assert.deepEqual(calls.capture, [1], 'capture is requested once, not every poll');

  for (let i = 0; i < GRAPH_NOTE_BATCH + 10; i += 1) queue.push(pack(i % 128, 64, 0, true));
  assert.equal(pump.tick(), GRAPH_NOTE_BATCH + 10, 'a backlog larger than one batch is drained in the same poll');

  sink.hasOutputValue = false;
  pump.tick();
  assert.deepEqual(calls.capture, [1, 0], 'deselecting the output stops capture');

  sink.hasOutputValue = true;
  pump.tick();
  pump.stop();
  assert.deepEqual(calls.capture, [1, 0, 1, 0], 'stop() turns capture off');
  assert.deepEqual(calls.freed, [16], 'and frees the staging buffer');
}

{
  const sink = fakeSink(true);
  const missing = new GraphNotePump(() => null, sink);
  assert.equal(missing.tick(), 0, 'no module yet is a no-op');
}

// A new module instance (re-init) gets its own capture request and buffer.
{
  const a = fakeModule();
  const b = fakeModule();
  let current = a.module;
  const sink = fakeSink(true);
  const pump = new GraphNotePump(() => current, sink);
  pump.tick();
  current = b.module;
  pump.tick();
  assert.deepEqual(a.calls.capture, [1]);
  assert.deepEqual(b.calls.capture, [1]);
}

// Switching to a worklet engine's note ring moves capture over at once and drains from the ring.
{
  const { module, calls } = fakeModule();
  const ring: number[] = [];
  const sourceCapture: boolean[] = [];
  const source = {
    setCapture: (enabled: boolean) => sourceCapture.push(enabled),
    drain: (out: Int32Array) => {
      const got = Math.min(out.length, ring.length);
      for (let i = 0; i < got; i += 1) out[i] = ring.shift() as number;
      return got;
    },
  };
  const sink = fakeSink(true);
  const pump = new GraphNotePump(() => module, sink);
  pump.tick();
  assert.deepEqual(calls.capture, [1]);
  const drainsBefore = calls.drains;

  pump.setSource(source);
  assert.deepEqual(calls.capture, [1, 0], 'the UI module stops capturing');
  assert.deepEqual(calls.freed, [16], 'and its staging buffer is freed');
  assert.deepEqual(sourceCapture, [true], 'the engine captures before the next poll');
  assert.equal(pump.isCapturing(), true);

  ring.push(pack(60, 90, 0, true));
  assert.equal(pump.tick(), 1, 'ring notes are forwarded');
  assert.equal(calls.drains, drainsBefore, 'the UI module is no longer drained');
  assert.deepEqual(sourceCapture, [true], 'capture is not re-requested');

  pump.setSource(null);
  assert.deepEqual(sourceCapture, [true, false], 'switching away releases the engine');
  pump.tick();
  assert.deepEqual(calls.capture, [1, 0, 1], 'and the UI module captures again');

  const idle = fakeSink(false);
  const idleCapture: boolean[] = [];
  const idlePump = new GraphNotePump(() => null, idle);
  idlePump.setSource({ setCapture: (enabled) => idleCapture.push(enabled), drain: () => 0 });
  assert.deepEqual(idleCapture, [], 'no output selected: nothing to capture');
}

// Performance notes are echoed raw unless a Note processor owns the performance.
{
  const out: number[][] = [];
  let processed = false;
  const echo = new PerformanceEcho(
    {
      noteOn: (channel, pitch, velocity) => out.push([0x90 | channel, pitch, Math.round(velocity * 127)]),
      noteOff: (channel, pitch) => out.push([0x80 | channel, pitch, 0]),
    },
    () => processed,
  );

  echo.echo(true, 0, 60, 0.8);
  echo.echo(false, 0, 60, 0);
  assert.deepEqual(
    out,
    [
      [0x90, 60, 102],
      [0x80, 60, 0],
    ],
    'unprocessed performance notes echo as played',
  );

  out.length = 0;
  processed = true;
  echo.echo(true, 0, 62, 0.8);
  echo.echo(false, 0, 62, 0);
  assert.deepEqual(out, [], 'a performance-listening processor suppresses the raw echo (the graph reports its output)');

  out.length = 0;
  processed = false;
  echo.echo(true, 0, 64, 0.8);
  processed = true; // a chorder is added while the key is held
  echo.echo(false, 0, 64, 0);
  assert.deepEqual(
    out,
    [
      [0x90, 64, 102],
      [0x80, 64, 0],
    ],
    'a raw note-on is always paired with its note-off',
  );

  out.length = 0;
  echo.echo(false, 0, 99, 0);
  assert.deepEqual(out, [], 'a note-off with no echoed note-on is not invented');

  out.length = 0;
  processed = false;
  echo.echo(true, 3, 70, 0.5);
  echo.echo(true, 4, 70, 0.5);
  echo.echo(false, 3, 70, 0);
  assert.deepEqual(
    out,
    [
      [0x93, 70, 64],
      [0x94, 70, 64],
      [0x83, 70, 0],
    ],
    'the same pitch on two channels is tracked separately',
  );
}

console.log('graphMidiOut tests passed');
