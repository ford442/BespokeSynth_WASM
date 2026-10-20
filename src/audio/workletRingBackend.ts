/**
 * Production AudioWorklet path (Option B): main-thread WASM graph fills a
 * SharedArrayBuffer ring; the worklet consumes and plays.
 */

import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import {
  createAudioRingBuffer,
  ringFramesAvailable,
  ringFramesFree,
  ringReadStats,
  ringWriteInterleaved,
  IDX_READ,
  IDX_RUNNING,
  IDX_WRITE,
  type AudioRingBuffer,
} from './ringBuffer';
import { workletSupport } from './audioMode';
import { LookaheadController } from './lookahead';

function ringPlayerUrl(): string {
  return new URL('audio/ringPlayer.worklet.js', window.location.href).toString();
}

export interface WorkletRingStats {
  /** Frames the producer currently keeps queued ahead of the worklet. */
  lookaheadFrames: number;
  /** Lookahead never decays below this once an underrun happened at a lower level. */
  lookaheadFloorFrames: number;
  depthFrames: number;
  capacityFrames: number;
  underrunFrames: number;
  /** Device latency after the producer (AudioContext base + output latency); worklet engine only. */
  outputLatencyMs?: number;
}

export interface WorkletRingBackend {
  start(): Promise<void>;
  stop(): void;
  dispose(): void;
  isRunning(): boolean;
  getContext(): AudioContext | null;
  getStats(): WorkletRingStats | null;
}

export function canUseWorkletRingBackend(): { ok: boolean; reason?: string } {
  return workletSupport();
}

export function createWorkletRingBackend(module: BespokeSynthModule): WorkletRingBackend {
  let ctx: AudioContext | null = null;
  let node: AudioWorkletNode | null = null;
  let ring: AudioRingBuffer | null = null;
  let running = false;
  let producerTimer: number | null = null;
  let heapBufferPtr = 0;
  let heapBufferFrames = 0;
  let lookahead: LookaheadController | null = null;
  const blockFrames = 128;

  const syncHealth = () => {
    if (!ring) return;
    const stats = ringReadStats(ring.header);
    module._bespoke_set_audio_queue_stats(stats.depth, stats.capacity, stats.underruns);
  };

  const ensureHeapBuffer = (frames: number): boolean => {
    if (heapBufferPtr && heapBufferFrames >= frames) return true;
    if (heapBufferPtr) module._free(heapBufferPtr);
    heapBufferPtr = module._malloc(frames * 2 * 4);
    heapBufferFrames = heapBufferPtr ? frames : 0;
    return heapBufferPtr !== 0;
  };

  const produce = () => {
    if (!running || !ring) return;
    const target = lookahead
      ? lookahead.update(
          ringReadStats(ring.header).underruns,
          performance.now(),
          document.visibilityState === 'hidden',
        )
      : ring.capacityFrames;
    while (ringFramesAvailable(ring.header) < target && ringFramesFree(ring.header) >= blockFrames) {
      if (!ensureHeapBuffer(blockFrames)) break; // heap exhausted: underrun rather than write to 0
      module._bespoke_process_audio_block(heapBufferPtr, blockFrames);
      // Emscripten replaces module.HEAPF32 when the heap grows, so read it after the call.
      const start = heapBufferPtr >> 2;
      const copy = module.HEAPF32.slice(start, start + blockFrames * 2);
      const written = ringWriteInterleaved(ring, copy, blockFrames);
      if (written < blockFrames) break;
    }
    syncHealth();
  };

  /** Builds the context, ring and node once; Stop keeps them, so Play again only restarts the producer. */
  const open = async () => {
    const gate = canUseWorkletRingBackend();
    if (!gate.ok) throw new Error(gate.reason || 'worklet backend unavailable');

    const context = new AudioContext({ latencyHint: 'interactive' });
    try {
      const sr = context.sampleRate;
      // Capacity is the growth ceiling (0.5 s); the live lookahead starts at ~50 ms.
      const capacityFrames = Math.max(8192, Math.floor(sr * 0.5));
      const newRing = createAudioRingBuffer(capacityFrames, sr);
      await context.audioWorklet.addModule(ringPlayerUrl());
      const newNode = new AudioWorkletNode(context, 'bespoke-ring-player', {
        numberOfOutputs: 1,
        outputChannelCount: [2],
        processorOptions: { sab: newRing.sab },
      });
      newNode.connect(context.destination);
      ctx = context;
      ring = newRing;
      node = newNode;
      lookahead = new LookaheadController(
        Math.max(blockFrames * 4, Math.floor(sr * 0.02)),
        capacityFrames - blockFrames,
        Math.floor(sr * 0.05),
        performance.now(),
      );
    } catch (error) {
      void context.close();
      throw error;
    }
  };

  let starting: Promise<void> | null = null;

  const startOnce = async () => {
    if (!ctx) await open();
    const context = ctx as AudioContext;
    if (context.state === 'suspended') await context.resume();

    // Only now does the UI module hand its audio to us: a failure above leaves it on SDL.
    module._bespoke_set_external_audio(1);
    module._bespoke_set_external_sample_rate(context.sampleRate);
    module._bespoke_set_audio_backend_id(1);
    module._bespoke_reset_audio_health();

    // Audio queued before the last Stop is stale. The worklet leaves the read index alone
    // while IDX_RUNNING is 0, so the producer may rewind its write index onto it.
    if (ring) Atomics.store(ring.header, IDX_WRITE, Atomics.load(ring.header, IDX_READ));

    // Prefill before the worklet starts consuming so the first quantum is not an underrun.
    running = true;
    produce();
    if (ring) Atomics.store(ring.header, IDX_RUNNING, 1);
    producerTimer = window.setInterval(produce, 4);
  };

  return {
    /** Idempotent while in flight (a double-click on Play starts one context). Rejects with nothing left on. */
    start() {
      if (running) return Promise.resolve();
      if (!starting) {
        starting = startOnce().finally(() => {
          starting = null;
        });
      }
      return starting;
    },

    stop() {
      running = false;
      if (producerTimer !== null) {
        clearInterval(producerTimer);
        producerTimer = null;
      }
      if (ring) Atomics.store(ring.header, IDX_RUNNING, 0);
      syncHealth();
    },

    dispose() {
      this.stop();
      try {
        node?.disconnect();
      } catch {
        // ignore
      }
      node = null;
      void ctx?.close();
      ctx = null;
      if (heapBufferPtr) {
        module._free(heapBufferPtr);
        heapBufferPtr = 0;
        heapBufferFrames = 0;
      }
      ring = null;
      lookahead = null;
      module._bespoke_set_external_audio(0);
      module._bespoke_set_audio_backend_id(0);
    },

    isRunning() {
      return running;
    },

    getContext() {
      return ctx;
    },

    getStats() {
      if (!ring) return null;
      const stats = ringReadStats(ring.header);
      return {
        lookaheadFrames: lookahead?.target ?? stats.capacity,
        lookaheadFloorFrames: lookahead?.floor ?? 0,
        depthFrames: stats.depth,
        capacityFrames: stats.capacity,
        underrunFrames: stats.underruns,
      };
    },
  };
}
