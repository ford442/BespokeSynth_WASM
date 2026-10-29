/**
 * Shared-memory layouts between the main thread and the AudioWorklet engine
 * (wasm/audio_worklet/engineProcessor.js, which mirrors these constants).
 *
 * Rings are single-producer/single-consumer: a 16-byte Int32 header
 * [write, read, capacity, dropped] followed by `capacity` 32-bit words, viewed
 * as Int32 or Float32. Indices are word offsets; a ring of fixed-size records
 * has a capacity that is a multiple of the record size, so records never wrap.
 */

export const RING_WRITE = 0;
export const RING_READ = 1;
export const RING_CAPACITY = 2;
export const RING_DROPPED = 3;
export const RING_HEADER_BYTES = 16;

export interface SabRing {
  sab: SharedArrayBuffer;
  header: Int32Array;
  words: Int32Array;
  floats: Float32Array;
}

export function createSabRing(capacityWords: number): SabRing {
  const sab = new SharedArrayBuffer(RING_HEADER_BYTES + capacityWords * 4);
  const ring = viewSabRing(sab);
  Atomics.store(ring.header, RING_CAPACITY, capacityWords);
  return ring;
}

export function viewSabRing(sab: SharedArrayBuffer): SabRing {
  return {
    sab,
    header: new Int32Array(sab, 0, 4),
    words: new Int32Array(sab, RING_HEADER_BYTES),
    floats: new Float32Array(sab, RING_HEADER_BYTES),
  };
}

/** Words the producer may still write (one record is kept empty to tell full from empty). */
export function ringFreeWords(ring: SabRing, recordWords = 1): number {
  const capacity = Atomics.load(ring.header, RING_CAPACITY);
  let used = Atomics.load(ring.header, RING_WRITE) - Atomics.load(ring.header, RING_READ);
  if (used < 0) used += capacity;
  return Math.max(0, capacity - recordWords - used);
}

/** Producer: append one 4-word event record; false (and counted as dropped) when full. */
export function ringPushRecord(ring: SabRing, a: number, b: number, c: number, d: number): boolean {
  if (ringFreeWords(ring, 4) < 4) {
    Atomics.add(ring.header, RING_DROPPED, 1);
    return false;
  }
  const capacity = Atomics.load(ring.header, RING_CAPACITY);
  const write = Atomics.load(ring.header, RING_WRITE);
  ring.words[write] = a;
  ring.words[write + 1] = b;
  ring.words[write + 2] = c;
  ring.words[write + 3] = d;
  Atomics.store(ring.header, RING_WRITE, write + 4 >= capacity ? 0 : write + 4);
  return true;
}

/** Consumer: copy up to `out.length` single words; returns the count. */
export function ringDrainWords(ring: SabRing, out: Int32Array): number {
  const capacity = Atomics.load(ring.header, RING_CAPACITY);
  let read = Atomics.load(ring.header, RING_READ);
  const write = Atomics.load(ring.header, RING_WRITE);
  let count = 0;
  while (read !== write && count < out.length) {
    out[count++] = ring.words[read];
    read = read + 1 >= capacity ? 0 : read + 1;
  }
  if (count > 0) Atomics.store(ring.header, RING_READ, read);
  return count;
}

/** AudioEngineEventType (wasm/include/BespokeWasm/AudioWorkletEngine.h). */
export const EVENT_NOTE = 1;
export const EVENT_SCRIPT_NOTE = 2;
export const EVENT_SCRIPT_CLEAR = 3;
export const EVENT_PARAM_CHANGE = 4;
/** Written by the engine processor from a Script channel record, never by the main thread. */
export const EVENT_SCRIPT_DIRECT_NOTE = 5;
export const EVENT_WORDS = 4;
/** EVENT_NOTE word a: word c holds the low 32 bits of the AudioContext frame it starts on. */
export const EVENT_NOTE_TIMED = 1 << 13;
/** 1024 records: a full second of dense QWERTY + script traffic between quanta. */
export const EVENT_RING_WORDS = 1024 * EVENT_WORDS;

/** Graph notes for the MIDI output, packed like _bespoke_drain_graph_notes. */
export const MIDI_OUT_RING_WORDS = 1024;
/** ~0.3 s of mono microphone at 48 kHz. */
export const INPUT_RING_WORDS = 16384;

/** Overwrite ring of mono (mid) output for the scope/FFT: Int32 [totalWritten], then floats. */
export const ANALYSIS_RING_FRAMES = 4096;

export interface AnalysisRing {
  sab: SharedArrayBuffer;
  header: Int32Array;
  samples: Float32Array;
}

export function createAnalysisRing(frames = ANALYSIS_RING_FRAMES): AnalysisRing {
  const sab = new SharedArrayBuffer(RING_HEADER_BYTES + frames * 4);
  return { sab, header: new Int32Array(sab, 0, 4), samples: new Float32Array(sab, RING_HEADER_BYTES) };
}

/**
 * Copies the samples written since `lastTotal` (at most the ring size, oldest first) into
 * `out`. Returns the new total and how many samples were copied.
 */
export function readAnalysisRing(
  ring: AnalysisRing,
  lastTotal: number,
  out: Float32Array,
): { total: number; count: number } {
  const total = Atomics.load(ring.header, 0);
  const capacity = ring.samples.length;
  let available = (total - lastTotal) | 0;
  if (available < 0 || available > capacity) available = capacity;
  const count = Math.min(available, out.length);
  let index = (total - count) | 0;
  for (let i = 0; i < count; i += 1) {
    out[i] = ring.samples[(index >>> 0) % capacity];
    index = (index + 1) | 0;
  }
  return { total, count };
}

/** Status block: Int32 flags/peaks, then Float64 telemetry (AudioEngineStatusSlot order). */
export const STATUS_INTS = 16;
/** Room for every AudioEngineStatusSlot (STATUS_SLOT_COUNT) and a few more. */
export const STATUS_DOUBLES = 40;
export const STATUS_BYTES = STATUS_INTS * 4 + STATUS_DOUBLES * 8;
export const ST_READY = 0;
export const ST_PEAK_L = 1;
export const ST_PEAK_R = 2;
export const ST_PEAK_INPUT = 3;
export const ST_MIDI_CAPTURE = 4;
export const ST_INPUT_ACTIVE = 5;
export const ST_SEQ = 6;

export const SLOT_BEAT = 0;
export const SLOT_CALLBACK_COUNT = 1;
export const SLOT_UNDERRUN_COUNT = 2;
export const SLOT_LAST_PROCESS_MS = 3;
export const SLOT_MAX_PROCESS_MS = 4;
export const SLOT_CPU_LOAD = 5;
export const SLOT_LAST_INTERVAL_MS = 6;
export const SLOT_NOTE_DROP_COUNT = 7;
export const SLOT_HEAP_BYTES = 8;
export const SLOT_MIDI_OUT_DROP_COUNT = 9;
export const SLOT_PEAK_L = 10;
export const SLOT_PEAK_R = 11;
export const SLOT_PEAK_INPUT = 12;
export const SLOT_SNAPSHOT_COUNT = 13;
export const SLOT_BPM = 14;
export const SLOT_PLAYING = 15;
/** Notes that came with a start frame, those whose frame was already rendered, and blocks split for them. */
export const SLOT_TIMED_NOTES = 16;
export const SLOT_LATE_NOTES = 17;
export const SLOT_SPLIT_BLOCKS = 18;
/** Script notes that came straight from a worker's channel. */
export const SLOT_DIRECT_SCRIPT_NOTES = 19;
/** Real-time violations inside processBlock; allocations/frees are only counted in the Debug flavor. */
export const SLOT_RT_ALLOCATIONS = 20;
export const SLOT_RT_FREES = 21;
export const SLOT_RT_LOCKS = 22;
export const SLOT_RT_COUNTS_ALLOCATIONS = 23;
/** AudioThreadScope of the engine's blocks: 3 = AudioWorkletGlobalScope. */
export const SLOT_ENGINE_SCOPE = 24;
export const SLOT_BLOCK_TIME_HISTOGRAM = 25;
/** Block process time over its budget: upper bounds of the first seven buckets (the last is open). */
export const BLOCK_TIME_BUCKET_LIMITS = Object.freeze([0.1, 0.25, 0.5, 0.75, 1, 1.5, 2]);
export const BLOCK_TIME_BUCKETS = BLOCK_TIME_BUCKET_LIMITS.length + 1;
export const STATUS_SLOT_COUNT = SLOT_BLOCK_TIME_HISTOGRAM + BLOCK_TIME_BUCKETS;
export const ENGINE_SCOPE_AUDIO_WORKLET = 3;

export interface StatusBlock {
  sab: SharedArrayBuffer;
  ints: Int32Array;
  doubles: Float64Array;
}

export function createStatusBlock(): StatusBlock {
  const sab = new SharedArrayBuffer(STATUS_BYTES);
  return {
    sab,
    ints: new Int32Array(sab, 0, STATUS_INTS),
    doubles: new Float64Array(sab, STATUS_INTS * 4, STATUS_DOUBLES),
  };
}

const bitsF = new Float32Array(1);
const bitsI = new Int32Array(bitsF.buffer);

/** Reads and clears a peak the worklet folds in with an atomic max of the float's bits. */
export function takePeak(status: StatusBlock, index: number): number {
  bitsI[0] = Atomics.exchange(status.ints, index, 0);
  return bitsF[0];
}

export function floatBits(value: number): number {
  bitsF[0] = value;
  return bitsI[0];
}

/**
 * A Script module's direct channel to the engine (one per running worker): the worker writes
 * note records into `ring` and reads the engine's transport from `clock`, so script timing does
 * not depend on the main thread. The engine processor registers both ({ type: 'scriptChannel' }),
 * stamps every record with the module it registered, and checks indices against its own view of
 * the ring. Mirrored by wasm/audio_worklet/engineProcessor.js and the worker bootstrap.
 *
 * Record (4 words): pitch | on << 7 | timed << 8, velocity bits, low 32 bits of the start frame, 0.
 */
export const SCRIPT_RING_WORDS = 256 * EVENT_WORDS;
export const SCRIPT_NOTE_ON = 1 << 7;
export const SCRIPT_NOTE_TIMED = 1 << 8;

/**
 * Clock: Int32 [seq, playing, closed, 0], then Float64 [beat, bpm, frameEnd, sampleRate] written
 * by the engine after every quantum (seq is odd while it writes). `beat` is the transport at
 * frameEnd, the AudioContext frame the next quantum starts on. The host sets `closed` when the
 * script stops; the processor then drops the channel.
 */
export const CLOCK_SEQ = 0;
export const CLOCK_PLAYING = 1;
export const CLOCK_CLOSED = 2;
export const CLOCK_BEAT = 0;
export const CLOCK_BPM = 1;
export const CLOCK_FRAME_END = 2;
export const CLOCK_SAMPLE_RATE = 3;
export const SCRIPT_CLOCK_BYTES = 16 + 4 * 8;

export interface ScriptClock {
  sab: SharedArrayBuffer;
  ints: Int32Array;
  doubles: Float64Array;
}

export interface ScriptChannel {
  ring: SabRing;
  clock: ScriptClock;
}

export function viewScriptClock(sab: SharedArrayBuffer): ScriptClock {
  return { sab, ints: new Int32Array(sab, 0, 4), doubles: new Float64Array(sab, 16, 4) };
}

export function createScriptChannel(): ScriptChannel {
  return { ring: createSabRing(SCRIPT_RING_WORDS), clock: viewScriptClock(new SharedArrayBuffer(SCRIPT_CLOCK_BYTES)) };
}

export interface ScriptClockReading {
  beat: number;
  bpm: number;
  playing: boolean;
  frameEnd: number;
  sampleRate: number;
}

/** Seqlock read; null while the engine is mid-write (try again next tick) or has not written yet. */
export function readScriptClock(clock: ScriptClock): ScriptClockReading | null {
  const seq = Atomics.load(clock.ints, CLOCK_SEQ);
  if (seq === 0 || (seq & 1) !== 0) return null;
  const reading = {
    beat: clock.doubles[CLOCK_BEAT],
    bpm: clock.doubles[CLOCK_BPM],
    playing: Atomics.load(clock.ints, CLOCK_PLAYING) === 1,
    frameEnd: clock.doubles[CLOCK_FRAME_END],
    sampleRate: clock.doubles[CLOCK_SAMPLE_RATE],
  };
  return Atomics.load(clock.ints, CLOCK_SEQ) === seq ? reading : null;
}

/** A running engine Script workers can talk to directly (the AudioWorklet engine). */
export interface ScriptEngineLink {
  /** Registers a new channel for `moduleId` with the engine; null when it cannot take one now. */
  openScriptChannel(moduleId: number): ScriptChannel | null;
  /** The script stopped: the engine drops the channel (its held notes go with ScriptClear). */
  closeScriptChannel(channel: ScriptChannel): void;
}
