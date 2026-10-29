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
  IDX_RUNNING,
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
    if (!ring || !module._bespoke_set_audio_queue_stats) return;
    const stats = ringReadStats(ring.header);
    module._bespoke_set_audio_queue_stats(stats.depth, stats.capacity, stats.underruns);
  };

  const ensureHeapBuffer = (frames: number) => {
    if (heapBufferPtr && heapBufferFrames >= frames) return;
    if (heapBufferPtr) module._free(heapBufferPtr);
    heapBufferFrames = frames;
    heapBufferPtr = module._malloc(frames * 2 * 4);
  };

  const produce = () => {
    if (!running || !ring || !module._bespoke_process_audio_block) return;
    const target = lookahead
      ? lookahead.update(
          ringReadStats(ring.header).underruns,
          performance.now(),
          document.visibilityState === 'hidden',
        )
      : ring.capacityFrames;
    while (ringFramesAvailable(ring.header) < target && ringFramesFree(ring.header) >= blockFrames) {
      ensureHeapBuffer(blockFrames);
      module._bespoke_process_audio_block(heapBufferPtr, blockFrames);
      // Emscripten replaces module.HEAPF32 when the heap grows, so read it after the call.
      const start = heapBufferPtr >> 2;
      const copy = module.HEAPF32.slice(start, start + blockFrames * 2);
      const written = ringWriteInterleaved(ring, copy, blockFrames);
      if (written < blockFrames) break;
    }
    syncHealth();
  };

  return {
    async start() {
      const gate = canUseWorkletRingBackend();
      if (!gate.ok) throw new Error(gate.reason || 'worklet backend unavailable');

      ctx = new AudioContext({ latencyHint: 'interactive' });
      const sr = ctx.sampleRate;
      // Capacity is the growth ceiling (0.5 s); the live lookahead starts at ~50 ms.
      const capacityFrames = Math.max(8192, Math.floor(sr * 0.5));
      ring = createAudioRingBuffer(capacityFrames, sr);
      lookahead = new LookaheadController(
        Math.max(blockFrames * 4, Math.floor(sr * 0.02)),
        capacityFrames - blockFrames,
        Math.floor(sr * 0.05),
        performance.now(),
      );

      module._bespoke_set_external_audio?.(1);
      module._bespoke_set_external_sample_rate?.(ctx.sampleRate);
      module._bespoke_set_audio_backend_id?.(1);
      module._bespoke_reset_audio_health?.();

      await ctx.audioWorklet.addModule(ringPlayerUrl());
      node = new AudioWorkletNode(ctx, 'bespoke-ring-player', {
        numberOfOutputs: 1,
        outputChannelCount: [2],
        processorOptions: { sab: ring.sab },
      });
      node.connect(ctx.destination);
      if (ctx.state === 'suspended') await ctx.resume();

      // Prefill before the worklet starts consuming so the first quantum is not an underrun.
      running = true;
      produce();
      Atomics.store(ring.header, IDX_RUNNING, 1);
      producerTimer = window.setInterval(produce, 4);
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
      module._bespoke_set_external_audio?.(0);
      module._bespoke_set_audio_backend_id?.(0);
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
