/**
 * SharedArrayBuffer stereo float ring for AudioWorklet playback.
 *
 * Int32 header (16 ints), then Float32 interleaved L/R samples.
 */

export const RING_HEADER_INTS = 16;
export const RING_HEADER_BYTES = RING_HEADER_INTS * 4;
export const RING_CHANNELS = 2;

export const IDX_WRITE = 0;
export const IDX_READ = 1;
export const IDX_UNDERRUN = 2;
export const IDX_CAPACITY = 3;
export const IDX_CHANNELS = 4;
export const IDX_SAMPLE_RATE = 5;
export const IDX_MAX_DEPTH = 6;
export const IDX_RUNNING = 7;

export interface AudioRingBuffer {
  sab: SharedArrayBuffer;
  header: Int32Array;
  samples: Float32Array;
  capacityFrames: number;
}

export function createAudioRingBuffer(capacityFrames: number, sampleRate: number): AudioRingBuffer {
  const bytes = RING_HEADER_BYTES + capacityFrames * RING_CHANNELS * 4;
  const sab = new SharedArrayBuffer(bytes);
  const header = new Int32Array(sab, 0, RING_HEADER_INTS);
  const samples = new Float32Array(sab, RING_HEADER_BYTES);
  Atomics.store(header, IDX_WRITE, 0);
  Atomics.store(header, IDX_READ, 0);
  Atomics.store(header, IDX_UNDERRUN, 0);
  Atomics.store(header, IDX_CAPACITY, capacityFrames);
  Atomics.store(header, IDX_CHANNELS, RING_CHANNELS);
  Atomics.store(header, IDX_SAMPLE_RATE, sampleRate | 0);
  Atomics.store(header, IDX_MAX_DEPTH, 0);
  Atomics.store(header, IDX_RUNNING, 0);
  return { sab, header, samples, capacityFrames };
}

export function ringFramesAvailable(header: Int32Array): number {
  const capacity = Atomics.load(header, IDX_CAPACITY);
  const write = Atomics.load(header, IDX_WRITE);
  const read = Atomics.load(header, IDX_READ);
  let avail = write - read;
  if (avail < 0) avail += capacity;
  return avail;
}

export function ringFramesFree(header: Int32Array): number {
  const capacity = Atomics.load(header, IDX_CAPACITY);
  // Leave one frame empty to distinguish full vs empty.
  return Math.max(0, capacity - 1 - ringFramesAvailable(header));
}

export function ringWriteInterleaved(
  ring: AudioRingBuffer,
  interleaved: Float32Array,
  frames: number,
): number {
  const { header, samples, capacityFrames } = ring;
  const free = ringFramesFree(header);
  const toWrite = Math.min(frames, free);
  if (toWrite <= 0) return 0;

  let write = Atomics.load(header, IDX_WRITE);
  for (let i = 0; i < toWrite; ++i) {
    const dst = write * RING_CHANNELS;
    const src = i * RING_CHANNELS;
    samples[dst] = interleaved[src] ?? 0;
    samples[dst + 1] = interleaved[src + 1] ?? 0;
    write += 1;
    if (write >= capacityFrames) write = 0;
  }
  Atomics.store(header, IDX_WRITE, write);

  const depth = ringFramesAvailable(header);
  const prevMax = Atomics.load(header, IDX_MAX_DEPTH);
  if (depth > prevMax) Atomics.store(header, IDX_MAX_DEPTH, depth);
  return toWrite;
}

export function ringReadStats(header: Int32Array): {
  depth: number;
  capacity: number;
  underruns: number;
  maxDepth: number;
} {
  return {
    depth: ringFramesAvailable(header),
    capacity: Atomics.load(header, IDX_CAPACITY),
    underruns: Atomics.load(header, IDX_UNDERRUN),
    maxDepth: Atomics.load(header, IDX_MAX_DEPTH),
  };
}
