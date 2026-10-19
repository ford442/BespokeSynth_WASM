/**
 * Default AudioWorklet path (Option A, docs/wasm-audio.md): a second, DSP-only WASM
 * instance (BespokeSynthWASM_audio) runs the graph inside the AudioWorklet, so a long
 * canvas frame cannot starve the speakers. The UI module keeps editing the patch;
 * every published snapshot is encoded and posted to the worklet, and performance and
 * script notes go through a SharedArrayBuffer ring the worklet drains each quantum.
 * Telemetry (beat, peaks, scope samples, graph notes for MIDI out, health) comes back
 * through shared memory and is mirrored into the UI module once per frame.
 */

import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import type { GraphNoteSource } from '../app/graphMidiOut';
import type { AudioHealth } from './audioHealth';
import { workletSupport } from './audioMode';
import {
  ANALYSIS_RING_FRAMES,
  EVENT_RING_WORDS,
  INPUT_RING_WORDS,
  MIDI_OUT_RING_WORDS,
  RING_DROPPED,
  SLOT_BEAT,
  SLOT_CALLBACK_COUNT,
  SLOT_CPU_LOAD,
  SLOT_HEAP_BYTES,
  SLOT_LAST_INTERVAL_MS,
  SLOT_LAST_PROCESS_MS,
  SLOT_MAX_PROCESS_MS,
  SLOT_NOTE_DROP_COUNT,
  SLOT_SNAPSHOT_COUNT,
  SLOT_UNDERRUN_COUNT,
  ST_INPUT_ACTIVE,
  ST_MIDI_CAPTURE,
  ST_PEAK_INPUT,
  ST_PEAK_L,
  ST_PEAK_R,
  ST_READY,
  createAnalysisRing,
  createSabRing,
  createStatusBlock,
  readAnalysisRing,
  ringDrainWords,
  ringPushRecord,
  takePeak,
  type AnalysisRing,
  type SabRing,
  type StatusBlock,
} from './engineShared';
import type { WorkletRingStats } from './workletRingBackend';

/** AudioHealth.backend for this path (0 SDL, 1 SAB ring, 2 tone POC). */
export const AUDIO_BACKEND_WORKLET_WASM = 3;
/** Web Audio's render quantum: the only buffering between the engine and the device. */
const QUANTUM_FRAMES = 128;
const READY_TIMEOUT_MS = 15000;

const assetUrl = (file: string) => new URL(`wasm/${file}`, window.location.href).toString();

export interface WorkletEngineBackend {
  start(): Promise<void>;
  /** Transport stop leaves the engine running: it renders silence until Play. */
  stop(): void;
  dispose(): void;
  isRunning(): boolean;
  getContext(): AudioContext | null;
  getStats(): WorkletRingStats | null;
  getHealth(): AudioHealth | null;
  /** Mono mic ring the capture worklet writes straight into (no main-thread hop). */
  getInputRing(): SharedArrayBuffer | null;
  setInputActive(active: boolean): void;
  /** Notes the engine played, for GraphNotePump. */
  graphNotes: GraphNoteSource;
  /** Once per frame: mirror beat, peaks and scope samples into the UI module. */
  syncToMain(): void;
  /** Seconds between a block leaving the engine and reaching the speakers. */
  outputLatencySeconds(): number;
}

let compiledEngine: Promise<WebAssembly.Module> | null = null;

/**
 * Compiles the engine .wasm on the main thread (the worklet has no fetch). Cached, so the app
 * can start it early and Play only waits for whatever is left.
 */
export function compileWorkletEngine(): Promise<WebAssembly.Module> {
  if (!compiledEngine) {
    const url = assetUrl('BespokeSynthWASM_audio.wasm');
    compiledEngine = (async () => {
      const response = await fetch(url);
      if (!response.ok) throw new Error(`audio engine ${url}: HTTP ${response.status}`);
      if (
        typeof WebAssembly.compileStreaming === 'function' &&
        response.headers.get('content-type')?.includes('wasm')
      ) {
        return WebAssembly.compileStreaming(response);
      }
      return WebAssembly.compile(await response.arrayBuffer());
    })();
    compiledEngine.catch(() => {
      compiledEngine = null;
    });
  }
  return compiledEngine;
}

export function createWorkletEngineBackend(module: BespokeSynthModule): WorkletEngineBackend {
  let ctx: AudioContext | null = null;
  let node: AudioWorkletNode | null = null;
  let running = false;
  let events: SabRing | null = null;
  let input: SabRing | null = null;
  let midiOut: SabRing | null = null;
  let analysis: AnalysisRing | null = null;
  let status: StatusBlock | null = null;
  let flushScheduled = false;
  let pendingReset = false;
  let samplesSent = 0;
  let analysisTotal = 0;
  const analysisScratch = new Float32Array(ANALYSIS_RING_FRAMES);
  let scratchPtr = 0;
  let scratchBytes = 0;
  let lastError: string | null = null;

  /** One grow-only main-heap scratch buffer for the small int/float hand-offs below. */
  const scratch = (bytes: number): number => {
    if (scratchPtr && scratchBytes >= bytes) return scratchPtr;
    if (scratchPtr) module._free(scratchPtr);
    scratchBytes = Math.max(bytes, 64);
    scratchPtr = module._malloc(scratchBytes);
    return scratchPtr;
  };

  /** Samples decoded in the UI module since the last call, under the same ids. */
  const sendNewSamples = () => {
    if (!node) return;
    const count = module._bespoke_get_sample_count();
    for (let id = samplesSent; id < count; id += 1) {
      const infoPtr = scratch(16);
      if (!module._bespoke_get_sample_info(id, infoPtr)) continue;
      const [channels, frames, rate, planarPtr] = new Int32Array(module.HEAPU8.buffer, infoPtr, 4);
      const pcm = new Float32Array(module.HEAPU8.buffer, planarPtr, channels * frames).slice();
      node.port.postMessage({ type: 'sample', id, channels, frames, rate, pcm }, [pcm.buffer]);
    }
    samplesSent = count;
  };

  const flushGraph = () => {
    flushScheduled = false;
    if (!node) return;
    // Samples first: the snapshot's sampler params refer to them by id.
    sendNewSamples();
    const lengthPtr = scratch(4);
    const ptr = module._bespoke_encode_audio_graph(lengthPtr);
    const length = new Int32Array(module.HEAPU8.buffer, lengthPtr, 1)[0];
    if (!ptr || length <= 0) return;
    const bytes = module.HEAPU8.slice(ptr, ptr + length);
    node.port.postMessage({ type: 'snapshot', bytes: bytes.buffer, reset: pendingReset }, [bytes.buffer]);
    pendingReset = false;
  };

  /** A knob drag publishes per pointer event; one microtask coalesces a burst into one post. */
  const scheduleFlush = () => {
    if (flushScheduled) return;
    flushScheduled = true;
    queueMicrotask(flushGraph);
  };

  const waitForReady = (target: AudioWorkletNode) =>
    new Promise<void>((resolve, reject) => {
      const timer = window.setTimeout(() => reject(new Error('audio engine did not start in time')), READY_TIMEOUT_MS);
      target.port.onmessage = (event: MessageEvent<{ type: string; message?: string }>) => {
        if (event.data.type === 'ready') {
          window.clearTimeout(timer);
          target.port.onmessage = onEngineMessage;
          resolve();
        } else if (event.data.type === 'error') {
          window.clearTimeout(timer);
          reject(new Error(event.data.message || 'audio engine failed'));
        }
      };
    });

  const onEngineMessage = (event: MessageEvent<{ type: string; message?: string }>) => {
    if (event.data.type === 'error') {
      lastError = event.data.message ?? 'audio engine error';
      console.error('Audio engine:', lastError);
    }
  };

  const outputLatencySeconds = () => (ctx ? (ctx.baseLatency || 0) + (ctx.outputLatency || 0) : 0);

  const graphNotes: GraphNoteSource = {
    setCapture(enabled: boolean) {
      if (status) Atomics.store(status.ints, ST_MIDI_CAPTURE, enabled ? 1 : 0);
    },
    drain(out: Int32Array) {
      return midiOut ? ringDrainWords(midiOut, out) : 0;
    },
  };

  const teardown = () => {
    running = false;
    if (module.bespokeAudioRemote) delete module.bespokeAudioRemote;
    module._bespoke_set_audio_remote?.(0);
    try {
      node?.disconnect();
    } catch {
      // ignore
    }
    if (node) node.port.onmessage = null;
    node = null;
    void ctx?.close();
    ctx = null;
  };

  return {
    async start() {
      if (running) return;
      const gate = workletSupport();
      if (!gate.ok) throw new Error(gate.reason || 'worklet backend unavailable');
      const wasmModule = await compileWorkletEngine();

      ctx = new AudioContext({ latencyHint: 'interactive' });
      events = createSabRing(EVENT_RING_WORDS);
      input = createSabRing(INPUT_RING_WORDS);
      midiOut = createSabRing(MIDI_OUT_RING_WORDS);
      analysis = createAnalysisRing();
      status = createStatusBlock();
      samplesSent = 0;
      analysisTotal = 0;

      try {
        await ctx.audioWorklet.addModule(assetUrl('BespokeSynthWASM_audio.js'));
        node = new AudioWorkletNode(ctx, 'bespoke-audio-engine', {
          numberOfInputs: 0,
          numberOfOutputs: 1,
          outputChannelCount: [2],
          processorOptions: {
            wasmModule,
            wireAbi: module._bespoke_get_audio_wire_abi(),
            events: events.sab,
            input: input.sab,
            midiOut: midiOut.sab,
            analysis: analysis.sab,
            status: status.sab,
          },
        });
        node.onprocessorerror = () => {
          lastError = 'audio engine processor threw; output is silent';
          console.error(lastError);
        };
        await waitForReady(node);
      } catch (error) {
        teardown();
        throw error;
      }

      module._bespoke_set_external_audio?.(1);
      module._bespoke_set_external_sample_rate?.(ctx.sampleRate);
      module._bespoke_set_audio_backend_id?.(AUDIO_BACKEND_WORKLET_WASM);
      module._bespoke_reset_audio_health?.();
      const eventRing = events;
      module.bespokeAudioRemote = {
        event: (type, a, b, c) => {
          ringPushRecord(eventRing, type, a, b, c);
        },
        graphChanged: (reset) => {
          if (reset) pendingReset = true;
          scheduleFlush();
        },
      };
      // From here every publish reaches the worklet; send what exists now right away.
      module._bespoke_set_audio_remote(1);
      flushGraph();

      node.connect(ctx.destination);
      if (ctx.state === 'suspended') await ctx.resume();
      running = true;
    },

    stop() {
      // Nothing to do: transport state travels with the snapshot.
    },

    dispose() {
      teardown();
      if (scratchPtr) module._free(scratchPtr);
      scratchPtr = 0;
      scratchBytes = 0;
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
      if (!status) return null;
      return {
        lookaheadFrames: QUANTUM_FRAMES,
        lookaheadFloorFrames: QUANTUM_FRAMES,
        depthFrames: 0,
        capacityFrames: QUANTUM_FRAMES,
        // Quanta that took longer than their own duration, each one quantum at risk.
        underrunFrames: status.doubles[SLOT_UNDERRUN_COUNT] * QUANTUM_FRAMES,
        outputLatencyMs: outputLatencySeconds() * 1000,
      };
    },

    getHealth() {
      if (!status || Atomics.load(status.ints, ST_READY) !== 1) return null;
      const d = status.doubles;
      return {
        backend: AUDIO_BACKEND_WORKLET_WASM,
        callbackCount: d[SLOT_CALLBACK_COUNT],
        underrunCount: d[SLOT_UNDERRUN_COUNT],
        externalUnderrunCount: 0,
        lastCallbackIntervalMs: d[SLOT_LAST_INTERVAL_MS],
        lastProcessTimeMs: d[SLOT_LAST_PROCESS_MS],
        maxProcessTimeMs: d[SLOT_MAX_PROCESS_MS],
        cpuLoad: d[SLOT_CPU_LOAD],
        queueDepthFrames: 0,
        maxQueueDepthFrames: 0,
        capacityFrames: QUANTUM_FRAMES,
        noteDropCount: d[SLOT_NOTE_DROP_COUNT],
        audioHeapBytes: d[SLOT_HEAP_BYTES],
        snapshotCount: d[SLOT_SNAPSHOT_COUNT],
        eventDropCount: events ? Atomics.load(events.header, RING_DROPPED) : 0,
        engineError: lastError ?? undefined,
      };
    },

    getInputRing() {
      return input?.sab ?? null;
    },

    setInputActive(active: boolean) {
      if (status) Atomics.store(status.ints, ST_INPUT_ACTIVE, active ? 1 : 0);
    },

    graphNotes,

    syncToMain() {
      if (!status || !analysis || !running) return;
      module._bespoke_set_remote_audio_status(
        status.doubles[SLOT_BEAT],
        takePeak(status, ST_PEAK_L),
        takePeak(status, ST_PEAK_R),
        takePeak(status, ST_PEAK_INPUT),
      );
      const { total, count } = readAnalysisRing(analysis, analysisTotal, analysisScratch);
      analysisTotal = total;
      if (count > 0) {
        const ptr = scratch(count * 4);
        module.HEAPF32.set(analysisScratch.subarray(0, count), ptr >> 2);
        module._bespoke_push_analysis_samples(ptr, count);
      }
    },

    outputLatencySeconds,
  };
}
