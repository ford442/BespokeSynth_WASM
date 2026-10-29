import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

export interface AudioHealth {
  backend: number;
  callbackCount: number;
  underrunCount: number;
  externalUnderrunCount: number;
  lastCallbackIntervalMs: number;
  lastProcessTimeMs: number;
  maxProcessTimeMs: number;
  cpuLoad: number;
  queueDepthFrames: number;
  maxQueueDepthFrames: number;
  capacityFrames: number;
  noteDropCount: number;
  /** WASM linear memory size in bytes (peak; memory never shrinks). Absent on older builds. */
  heapBytes?: number;
  /** Worklet engine only: its own WASM heap (heapBytes is then the UI module's). */
  audioHeapBytes?: number;
  /** Worklet engine only: snapshots it has applied. */
  snapshotCount?: number;
  /** Worklet engine only: note/script events dropped because the event ring was full. */
  eventDropCount?: number;
  /**
   * Worklet engine only: blocks the UI module rendered on the main thread. Stays 0 while the
   * worklet engine plays (bespoke_set_audio_remote makes the UI module refuse to render).
   */
  mainThreadBlocks?: number;
  engineError?: string;
  /**
   * Blocks by process time over their budget, buckets ending at 0.1, 0.25, 0.5, 0.75, 1, 1.5, 2
   * and above (BLOCK_TIME_BUCKET_LIMITS). The last two buckets are overruns.
   */
  blockTimeHistogram?: number[];
  /** Heap calls and RtMutex locks inside the audio block; heap calls are only counted when rtCountsAllocations (Debug). */
  rtAllocations?: number;
  rtFrees?: number;
  rtLocks?: number;
  rtCountsAllocations?: boolean;
  /** Worklet engine only: notes that came with a start frame, those already late, blocks split for them. */
  timedNotes?: number;
  lateNotes?: number;
  splitBlocks?: number;
  /** Worklet engine only: Script notes a worker wrote straight to the engine. */
  directScriptNotes?: number;
  /** Worklet engine only: the global scope its blocks run in (3 = AudioWorkletGlobalScope). */
  engineScope?: number;
  /** Delay added to live notes so they start on their frame (NoteClock). */
  noteMarginMs?: number;
}

export function readAudioHealth(module: BespokeSynthModule | null): AudioHealth | null {
  if (!module) return null;
  const ptr = module._bespoke_get_audio_health_json();
  if (!ptr) return null;
  try {
    return JSON.parse(module.UTF8ToString(ptr)) as AudioHealth;
  } catch {
    return null;
  }
}

export function backendName(id: number): string {
  switch (id) {
    case 1:
      return 'worklet-ring';
    case 2:
      return 'poc';
    case 3:
      return 'worklet-wasm';
    default:
      return 'sdl';
  }
}
