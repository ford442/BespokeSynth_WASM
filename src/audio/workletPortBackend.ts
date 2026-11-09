/**
 * AudioWorklet output without SharedArrayBuffer (Option C). Same shape as the SAB ring backend:
 * the main-thread WASM graph renders ahead of the device up to an adaptive lookahead. The blocks
 * travel to the worklet as transferred Float32Arrays over its MessagePort instead of a shared
 * ring, so this works on any page that has AudioWorkletNode, cross-origin isolated or not.
 *
 * This is what replaces SDL2's ScriptProcessorNode on hosts that cannot send COOP/COEP. The
 * queue depth the producer sees comes from the worklet's stats messages (a few per 100 ms), so
 * it starts with a deeper lookahead than the ring.
 */

import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { AUDIO_BACKEND_PORT } from './audioHealth';
import { portWorkletSupport } from './audioMode';
import { LookaheadController } from './lookahead';
import type { WorkletRingBackend } from './workletRingBackend';

const BLOCK_FRAMES = 128;
/** Blocks rendered per producer tick at most, so a long stall catches up in steps, not one long task. */
const MAX_BLOCKS_PER_TICK = 64;
const PRODUCER_INTERVAL_MS = 4;

function portPlayerUrl(): string {
  return new URL('audio/portPlayer.worklet.js', window.location.href).toString();
}

export function canUseWorkletPortBackend(): { ok: boolean; reason?: string } {
  const support = portWorkletSupport();
  if (!support.ok) return support;
  if (typeof AudioContext === 'undefined') return { ok: false, reason: 'AudioContext unavailable' };
  return { ok: true };
}

interface PlayerStats {
  type: 'stats';
  epoch: number;
  consumed: number;
  underruns: number;
}

export function createWorkletPortBackend(module: BespokeSynthModule): WorkletRingBackend {
  let ctx: AudioContext | null = null;
  let node: AudioWorkletNode | null = null;
  let running = false;
  let producerTimer: number | null = null;
  let heapBufferPtr = 0;
  let heapBufferFrames = 0;
  let lookahead: LookaheadController | null = null;
  let capacityFrames = 0;
  /** Bumped on every Play: stats and chunks of an older run are dropped on both sides. */
  let epoch = 0;
  let framesSent = 0;
  let framesConsumed = 0;
  let underrunFrames = 0;

  const syncHealth = () => {
    module._bespoke_set_audio_queue_stats(Math.max(0, framesSent - framesConsumed), capacityFrames, underrunFrames);
  };

  const ensureHeapBuffer = (frames: number): boolean => {
    if (heapBufferPtr && heapBufferFrames >= frames) return true;
    if (heapBufferPtr) module._free(heapBufferPtr);
    heapBufferPtr = module._malloc(frames * 2 * 4);
    heapBufferFrames = heapBufferPtr ? frames : 0;
    return heapBufferPtr !== 0;
  };

  const produce = () => {
    if (!running || !node || !lookahead) return;
    const target = lookahead.update(underrunFrames, performance.now(), document.visibilityState === 'hidden');
    const depth = Math.max(0, framesSent - framesConsumed);
    const blocks = Math.min(MAX_BLOCKS_PER_TICK, Math.floor((target - depth) / BLOCK_FRAMES));
    if (blocks > 0 && ensureHeapBuffer(BLOCK_FRAMES)) {
      const chunk = new Float32Array(blocks * BLOCK_FRAMES * 2);
      for (let b = 0; b < blocks; b += 1) {
        module._bespoke_process_audio_block(heapBufferPtr, BLOCK_FRAMES);
        // Emscripten replaces module.HEAPF32 when the heap grows, so read it after the call.
        const start = heapBufferPtr >> 2;
        chunk.set(module.HEAPF32.subarray(start, start + BLOCK_FRAMES * 2), b * BLOCK_FRAMES * 2);
      }
      node.port.postMessage({ type: 'chunk', epoch, samples: chunk }, [chunk.buffer]);
      framesSent += blocks * BLOCK_FRAMES;
    }
    syncHealth();
  };

  const onPlayerMessage = (event: MessageEvent<PlayerStats | { type: string }>) => {
    const data = event.data;
    if (data.type !== 'stats') return;
    const stats = data as PlayerStats;
    if (stats.epoch !== epoch) return;
    framesConsumed = stats.consumed;
    underrunFrames = stats.underruns;
  };

  /** Builds the context and node once; Stop keeps them, so Play again only restarts the producer. */
  const open = async () => {
    const gate = canUseWorkletPortBackend();
    if (!gate.ok) throw new Error(gate.reason || 'worklet port backend unavailable');

    const context = new AudioContext({ latencyHint: 'interactive' });
    try {
      const sr = context.sampleRate;
      await context.audioWorklet.addModule(portPlayerUrl());
      const newNode = new AudioWorkletNode(context, 'bespoke-port-player', {
        numberOfInputs: 0,
        numberOfOutputs: 1,
        outputChannelCount: [2],
      });
      newNode.port.onmessage = onPlayerMessage;
      newNode.connect(context.destination);
      ctx = context;
      node = newNode;
      capacityFrames = Math.max(8192, Math.floor(sr * 0.5));
      lookahead = new LookaheadController(
        Math.max(BLOCK_FRAMES * 8, Math.floor(sr * 0.03)),
        capacityFrames - BLOCK_FRAMES,
        Math.floor(sr * 0.08),
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
    module._bespoke_set_audio_backend_id(AUDIO_BACKEND_PORT);
    module._bespoke_reset_audio_health();

    epoch += 1;
    framesSent = 0;
    framesConsumed = 0;
    node?.port.postMessage({ type: 'run', running: true, epoch });
    running = true;
    // Prefill so the first quanta find audio waiting.
    produce();
    producerTimer = window.setInterval(produce, PRODUCER_INTERVAL_MS);
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
      // Audio queued before Stop is stale; the worklet drops it and goes silent.
      node?.port.postMessage({ type: 'run', running: false, epoch });
      syncHealth();
    },

    dispose() {
      this.stop();
      try {
        node?.disconnect();
      } catch {
        // ignore
      }
      if (node) node.port.onmessage = null;
      node = null;
      void ctx?.close();
      ctx = null;
      if (heapBufferPtr) {
        module._free(heapBufferPtr);
        heapBufferPtr = 0;
        heapBufferFrames = 0;
      }
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
      if (!ctx || !lookahead) return null;
      return {
        lookaheadFrames: lookahead.target,
        lookaheadFloorFrames: lookahead.floor,
        depthFrames: Math.max(0, framesSent - framesConsumed),
        capacityFrames,
        underrunFrames,
        outputLatencyMs: ((ctx.baseLatency || 0) + (ctx.outputLatency || 0)) * 1000,
      };
    },
  };
}
