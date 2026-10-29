import assert from 'node:assert/strict';

// The worklet file is plain JS that expects AudioWorkletGlobalScope globals.
interface PortStub {
  posted: Array<{ type: string; samples: Float32Array }>;
  onmessage: ((event: { data: unknown }) => void) | null;
  postMessage(message: { type: string; samples: Float32Array }, transfer?: unknown[]): void;
}
type Processor = { port: PortStub; process(inputs: Float32Array[][]): boolean };
type ProcessorCtor = new (options?: { processorOptions?: { chunkFrames?: number } }) => Processor;

const registered = new Map<string, ProcessorCtor>();
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
g.registerProcessor = (name: string, ctor: ProcessorCtor) => registered.set(name, ctor);

await import('../../src/audio/processors/micCapture.worklet.js');
const Mic = registered.get('bespoke-mic-capture');
assert.ok(Mic, 'registers bespoke-mic-capture');

const QUANTUM = 128;
let next = 0;
const quantum = (): Float32Array[][] => [[Float32Array.from({ length: QUANTUM }, () => next++)]];
const flatten = (proc: Processor): number[] => proc.port.posted.flatMap((m) => Array.from(m.samples));

// Chunk size that is a multiple of the render quantum: one message per two quanta.
{
  const proc = new Mic({ processorOptions: { chunkFrames: 256 } });
  next = 0;
  assert.equal(proc.process(quantum()), true);
  assert.equal(proc.port.posted.length, 0, 'half a chunk is held back');
  proc.process(quantum());
  assert.equal(proc.port.posted.length, 1, 'full chunk is posted');
  assert.equal(proc.port.posted[0].type, 'chunk');
  assert.equal(proc.port.posted[0].samples.length, 256);
  assert.deepEqual(flatten(proc), Array.from({ length: 256 }, (_, i) => i));
}

// Chunk size that does not divide the quantum: samples carry across chunk edges in order.
{
  const proc = new Mic({ processorOptions: { chunkFrames: 192 } });
  next = 0;
  for (let i = 0; i < 6; ++i) proc.process(quantum()); // 768 frames = 4 chunks
  assert.equal(proc.port.posted.length, 4);
  assert.ok(proc.port.posted.every((m) => m.samples.length === 192));
  assert.deepEqual(flatten(proc), Array.from({ length: 768 }, (_, i) => i), 'no gaps, repeats or reordering');
}

// Defaults and degenerate inputs.
{
  const proc = new Mic();
  next = 0;
  for (let i = 0; i < 4; ++i) proc.process(quantum());
  assert.equal(proc.port.posted.length, 1, 'default chunk is 512 frames');
  assert.equal(proc.port.posted[0].samples.length, 512);

  assert.equal(proc.process([[]]), true, 'input with no channels keeps the processor alive');
  assert.equal(proc.process([]), true, 'no input at all keeps the processor alive');
  assert.equal(proc.port.posted.length, 1, 'and posts nothing');

  const bogus = new Mic({ processorOptions: { chunkFrames: 0 } });
  for (let i = 0; i < 4; ++i) bogus.process(quantum());
  assert.equal(bogus.port.posted[0]?.samples.length, 512, 'non-positive chunkFrames falls back to the default');
}

// Stop message ends the processor.
{
  const proc = new Mic();
  proc.port.onmessage?.({ data: { type: 'stop' } });
  assert.equal(proc.process(quantum()), false, 'stopped processor returns false so the browser drops it');
}

console.log('micCaptureWorklet.test.ts passed');
