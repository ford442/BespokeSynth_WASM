/**
 * Production AudioWorklet path (Option B): main-thread WASM graph fills a
 * SharedArrayBuffer ring; the worklet consumes and plays.
 */

import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import {
  createAudioRingBuffer,
  ringFramesFree,
  ringReadStats,
  ringWriteInterleaved,
  IDX_RUNNING,
  type AudioRingBuffer,
} from './ringBuffer';
import { crossOriginIsolatedOk, sharedArrayBufferAvailable } from './audioMode';

function ringPlayerUrl(): string {
  return new URL('audio/ringPlayer.worklet.js', window.location.href).toString();
}

export interface WorkletRingBackend {
  start(): Promise<void>;
  stop(): void;
  dispose(): void;
  isRunning(): boolean;
  getContext(): AudioContext | null;
}

export function canUseWorkletRingBackend(): { ok: boolean; reason?: string } {
  if (typeof AudioWorkletNode === 'undefined') {
    return { ok: false, reason: 'AudioWorkletNode unavailable' };
  }
  if (!sharedArrayBufferAvailable()) {
    return { ok: false, reason: 'SharedArrayBuffer unavailable' };
  }
  if (!crossOriginIsolatedOk()) {
    return {
      ok: false,
      reason: 'crossOriginIsolated is false (need COOP/COEP headers for SAB)',
    };
  }
  return { ok: true };
}

export function createWorkletRingBackend(module: BespokeSynthModule): WorkletRingBackend {
  let ctx: AudioContext | null = null;
  let node: AudioWorkletNode | null = null;
  let ring: AudioRingBuffer | null = null;
  let running = false;
  let producerTimer: number | null = null;
  let heapBufferPtr = 0;
  let heapBufferFrames = 0;
  const blockFrames = 128;

  const syncHealth = () => {
    if (!ring || !module._bespoke_set_audio_queue_stats) return;
    const stats = ringReadStats(ring.header);
    module._bespoke_set_audio_queue_stats(stats.depth, stats.capacity, stats.underruns);
  };

  const getHeapF32 = (): Float32Array => {
    const m = module as BespokeSynthModule & {
      HEAPF32?: Float32Array;
      HEAP8?: Int8Array;
      wasmMemory?: WebAssembly.Memory;
    };
    if (m.HEAPF32) return m.HEAPF32;
    if (m.wasmMemory?.buffer) return new Float32Array(m.wasmMemory.buffer);
    if (m.HEAP8?.buffer) return new Float32Array(m.HEAP8.buffer);
    throw new Error('WASM heap Float32 view unavailable');
  };

  const ensureHeapBuffer = (frames: number) => {
    if (heapBufferPtr && heapBufferFrames >= frames) return;
    const alloc = module._malloc ?? (module as unknown as { malloc?: (n: number) => number }).malloc;
    const freeFn = module._free ?? (module as unknown as { free?: (p: number) => void }).free;
    if (typeof alloc !== 'function') {
      throw new Error('WASM malloc is not exported; rebuild with _malloc/_free in EXPORTED_FUNCTIONS');
    }
    if (heapBufferPtr && typeof freeFn === 'function') freeFn(heapBufferPtr);
    heapBufferFrames = frames;
    heapBufferPtr = alloc(frames * 2 * 4);
  };

  const produce = () => {
    if (!running || !ring || !module._bespoke_process_audio_block) return;
    // Keep the ring reasonably full (aim > 25% capacity).
    while (ringFramesFree(ring.header) >= blockFrames) {
      ensureHeapBuffer(blockFrames);
      module._bespoke_process_audio_block(heapBufferPtr, blockFrames);
      const heap = getHeapF32();
      const start = heapBufferPtr >> 2;
      const copy = heap.slice(start, start + blockFrames * 2);
      const written = ringWriteInterleaved(ring, copy, blockFrames);
      if (written < blockFrames) break;
    }
    syncHealth();
  };

  return {
    async start() {
      const gate = canUseWorkletRingBackend();
      if (!gate.ok) throw new Error(gate.reason || 'worklet backend unavailable');

      ctx = new AudioContext();
      const capacityFrames = Math.max(4096, Math.floor(ctx.sampleRate * 0.25));
      ring = createAudioRingBuffer(capacityFrames, ctx.sampleRate);

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

      Atomics.store(ring.header, IDX_RUNNING, 1);
      running = true;

      // Prefill before transport starts sounding.
      produce();
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
      module._bespoke_set_external_audio?.(0);
      module._bespoke_set_audio_backend_id?.(0);
    },

    isRunning() {
      return running;
    },

    getContext() {
      return ctx;
    },
  };
}
