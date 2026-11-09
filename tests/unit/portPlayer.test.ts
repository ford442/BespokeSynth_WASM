import assert from 'node:assert/strict';

// The worklet file is plain JS that expects AudioWorkletGlobalScope globals.
interface Stats {
  type: string;
  epoch: number;
  consumed: number;
  underruns: number;
}
interface PortStub {
  posted: Stats[];
  onmessage: ((event: { data: unknown }) => void) | null;
  postMessage(message: Stats): void;
}
type Processor = { port: PortStub; process(inputs: Float32Array[][], outputs: Float32Array[][]): boolean };

const registered = new Map<string, new () => Processor>();
const g = globalThis as Record<string, unknown>;
g.AudioWorkletProcessor = class {
  port: PortStub = {
    posted: [],
    onmessage: null,
    postMessage(message) {
      this.posted.push(message);
    },
  };
};
g.registerProcessor = (name: string, ctor: new () => Processor) => registered.set(name, ctor);

await import('../../src/audio/processors/portPlayer.worklet.js');
const Player = registered.get('bespoke-port-player');
assert.ok(Player, 'registers bespoke-port-player');

const QUANTUM = 128;
const send = (proc: Processor, data: unknown) => proc.port.onmessage?.({ data });
const outputs = (): Float32Array[][] => [[new Float32Array(QUANTUM), new Float32Array(QUANTUM)]];
/** Stereo chunk whose left channel counts up from `from` and right is its negative. */
const chunk = (from: number, frames: number): Float32Array => {
  const samples = new Float32Array(frames * 2);
  for (let i = 0; i < frames; ++i) {
    samples[i * 2] = from + i;
    samples[i * 2 + 1] = -(from + i);
  }
  return samples;
};
const play = (proc: Processor): Float32Array[] => {
  const out = outputs();
  assert.equal(proc.process([], out), true, 'keeps the node alive');
  return out[0];
};

// Silent until told to run; chunks sent before 'run' are ignored.
{
  const proc = new Player();
  send(proc, { type: 'chunk', epoch: 0, samples: chunk(1, QUANTUM) });
  const [l] = play(proc);
  assert.ok(l.every((v) => v === 0), 'silence while stopped');
  assert.equal(proc.port.posted.length, 0, 'posts nothing while stopped');
}

// Plays chunks in order across quantum boundaries, both channels.
{
  const proc = new Player();
  send(proc, { type: 'run', running: true, epoch: 1 });
  send(proc, { type: 'chunk', epoch: 1, samples: chunk(0, 200) });
  send(proc, { type: 'chunk', epoch: 1, samples: chunk(200, 312) });
  const heard: number[] = [];
  const heardR: number[] = [];
  for (let q = 0; q < 4; ++q) {
    const [l, r] = play(proc);
    heard.push(...l);
    heardR.push(...r);
  }
  assert.deepEqual(heard, Array.from({ length: 512 }, (_, i) => i), 'no gaps, repeats or reordering');
  assert.ok(heardR.every((v, i) => v === -heard[i]), 'right channel follows');
}

// Counters: consumed frames, underruns once primed, stats every 4 quanta, epoch echoed.
{
  const proc = new Player();
  send(proc, { type: 'run', running: true, epoch: 7 });
  play(proc);
  assert.equal(proc.port.posted.length, 0);
  for (let q = 0; q < 3; ++q) play(proc);
  assert.equal(proc.port.posted.length, 1, 'one stats message per 4 quanta');
  assert.deepEqual(proc.port.posted[0], { type: 'stats', epoch: 7, consumed: 0, underruns: 0 });
  assert.equal(proc.port.posted[0].underruns, 0, 'silence before the first chunk is startup, not an underrun');

  send(proc, { type: 'chunk', epoch: 7, samples: chunk(1, 192) });
  play(proc); // 128 real frames
  const [l] = play(proc); // 64 real frames, then 64 of silence
  assert.equal(l[63], 192, 'last real sample');
  assert.equal(l[64], 0, 'then silence');
  play(proc);
  play(proc);
  const stats = proc.port.posted.at(-1) as Stats;
  assert.equal(stats.consumed, 192, 'frames played');
  assert.equal(stats.underruns, 64 + 128 + 128, 'every silent frame after priming is an underrun');
}

// A new run drops stale audio and stale-epoch chunks.
{
  const proc = new Player();
  send(proc, { type: 'run', running: true, epoch: 1 });
  send(proc, { type: 'chunk', epoch: 1, samples: chunk(1, 1000) });
  send(proc, { type: 'run', running: true, epoch: 2 });
  send(proc, { type: 'chunk', epoch: 1, samples: chunk(500, 128) });
  const [stale] = play(proc);
  assert.ok(stale.every((v) => v === 0), 'nothing left of the old run, old-epoch chunks dropped');
  send(proc, { type: 'chunk', epoch: 2, samples: chunk(9, 128) });
  const [fresh] = play(proc);
  assert.equal(fresh[0], 9, 'the new run plays its own chunk');
  assert.equal(fresh[QUANTUM - 1], 9 + QUANTUM - 1);
}

// Stop clears the queue and goes silent.
{
  const proc = new Player();
  send(proc, { type: 'run', running: true, epoch: 1 });
  send(proc, { type: 'chunk', epoch: 1, samples: chunk(1, 1000) });
  send(proc, { type: 'run', running: false, epoch: 1 });
  const [l] = play(proc);
  assert.ok(l.every((v) => v === 0));
  send(proc, { type: 'run', running: true, epoch: 2 });
  assert.ok(play(proc)[0].every((v) => v === 0), 'audio queued before Stop never plays after Play');
}

console.log('portPlayer tests passed');
