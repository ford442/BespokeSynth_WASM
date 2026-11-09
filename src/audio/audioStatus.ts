/**
 * The real audio output state, as the browser reports it. Neither SDL2 nor the worklet backends
 * can tell "a device is playing" from "the context exists": SDL2's driver reports success without
 * a device, and an AudioContext happily runs with nothing behind it. This module combines what is
 * observable:
 *
 *  - the AudioContext state (`suspended` until a gesture, `running`, `interrupted`, `closed`),
 *  - whether its clock advances (a running context whose `currentTime` stands still has no device),
 *  - the browser's output device list (`enumerateDevices`, trusted only where the browser lists
 *    outputs without permission, i.e. Chromium; Firefox and Safari answer "unknown"),
 *  - the context's `error` event and any error from starting the backend.
 */

import type { AudioBackendId } from './audioMode';

export type AudioStatusLevel = 'idle' | 'ok' | 'warn' | 'error';

export type AudioStatusState =
  | 'idle'
  | 'starting'
  | 'running'
  | 'suspended'
  | 'interrupted'
  | 'stalled'
  | 'closed'
  | 'no-device'
  | 'failed';

export type AudioContextStateLike = AudioContextState | 'interrupted';

/** Everything the verdict is made from. All of it is observation, none of it a guess. */
export interface AudioStatusInput {
  backend: AudioBackendId;
  /** Play was pressed (and Stop not since). */
  playRequested: boolean;
  /** The backend threw while starting. */
  startError: string | null;
  /** The context raised its `error` event (Chromium: the output device failed). */
  deviceError: string | null;
  /** null: no AudioContext exists yet. */
  contextState: AudioContextStateLike | null;
  /** Whether `currentTime` moved in step with the wall clock; null until measured. */
  clockAdvancing: boolean | null;
  /** Output devices the browser lists; null when it does not say. */
  outputDevices: number | null;
  sampleRate: number | null;
  latencyMs: number | null;
}

export interface AudioStatus extends AudioStatusInput {
  level: AudioStatusLevel;
  state: AudioStatusState;
  /** One line for the header chip and the banner. */
  message: string;
}

const BACKEND_LABEL: Record<AudioBackendId, string> = {
  worklet: 'AudioWorklet engine',
  ring: 'AudioWorklet (shared ring)',
  port: 'AudioWorklet (message port)',
  sdl: 'SDL2 (ScriptProcessorNode)',
};

export function audioBackendLabel(backend: AudioBackendId): string {
  return BACKEND_LABEL[backend];
}

export function evaluateAudioStatus(input: AudioStatusInput): AudioStatus {
  const make = (level: AudioStatusLevel, state: AudioStatusState, message: string): AudioStatus => ({
    ...input,
    level,
    state,
    message,
  });
  const { contextState } = input;

  if (input.startError) return make('error', 'failed', `Audio failed to start: ${input.startError}`);
  if (input.deviceError) return make('error', 'failed', `Audio output error: ${input.deviceError}`);

  if (contextState === null) {
    if (input.outputDevices === 0) {
      return make('warn', 'no-device', 'No audio output device found: there will be no sound');
    }
    return input.playRequested
      ? make('idle', 'starting', 'Starting audio...')
      : make('idle', 'idle', 'Audio idle: press Play to start');
  }

  if (contextState === 'closed') return make('error', 'closed', 'Audio context closed: reload to restore sound');
  if (input.outputDevices === 0) {
    return make('error', 'no-device', 'No audio output device available: output is silent');
  }
  if (contextState === 'suspended') {
    return input.playRequested
      ? make('warn', 'suspended', 'Audio is suspended by the browser: click the page or press Play to resume')
      : make('idle', 'idle', 'Audio idle: press Play to start');
  }
  if (contextState === 'interrupted') {
    return make('warn', 'interrupted', 'Audio interrupted by the system (call, lock screen or device change)');
  }
  if (input.clockAdvancing === false) {
    return make('error', 'stalled', 'Audio clock is not advancing: no usable output device, so output is silent');
  }

  const rate = input.sampleRate ? ` · ${(input.sampleRate / 1000).toFixed(input.sampleRate % 1000 ? 1 : 0)} kHz` : '';
  const latency = input.latencyMs && input.latencyMs > 0 ? ` · ${Math.round(input.latencyMs)} ms output latency` : '';
  return make('ok', 'running', `Audio running${rate}${latency}`);
}

/** `navigator.mediaDevices` is missing on insecure origins, whatever the DOM typings say. */
function mediaDevices(): MediaDevices | undefined {
  return typeof navigator === 'undefined' ? undefined : (navigator as { mediaDevices?: MediaDevices }).mediaDevices;
}

/**
 * Whether `enumerateDevices` lists audio outputs here. Chromium does (without permission, when a
 * device exists) and also has AudioContext.setSinkId; Firefox and Safari list no outputs until the
 * microphone is granted, so an empty list there says nothing.
 */
export function browserListsOutputDevices(): boolean {
  return (
    typeof AudioContext !== 'undefined' &&
    'setSinkId' in AudioContext.prototype &&
    typeof mediaDevices()?.enumerateDevices === 'function'
  );
}

export interface AudioStatusMonitorOptions {
  getBackend: () => AudioBackendId;
  /** The context that plays now: the worklet backend's, or SDL2's own; null before Play. */
  getContext: () => AudioContext | null;
  getPlayRequested: () => boolean;
}

/** Wall time over which `currentTime` must show movement before the clock counts as stalled. */
const CLOCK_WINDOW_MS = 900;
/** Fraction of real time the context clock must keep up with. */
const CLOCK_MIN_RATE = 0.25;
const POLL_INTERVAL_MS = 1000;

export class AudioStatusMonitor {
  private readonly listeners = new Set<(status: AudioStatus) => void>();
  private context: AudioContext | null = null;
  private clockSample: { wall: number; audio: number } | null = null;
  private clockAdvancing: boolean | null = null;
  private outputDevices: number | null = null;
  private deviceError: string | null = null;
  private startError: string | null = null;
  private timer: number | null = null;
  private lastJson = '';
  private current: AudioStatus;
  private readonly onContextEvent = () => this.poll();
  private readonly onContextError = (event: Event) => {
    const detail = (event as Event & { message?: string }).message;
    this.deviceError = detail || 'the output device reported an error';
    this.poll();
  };
  private readonly onDeviceChange = () => void this.refreshDevices();

  private readonly options: AudioStatusMonitorOptions;

  constructor(options: AudioStatusMonitorOptions) {
    this.options = options;
    this.current = evaluateAudioStatus(this.collect());
    this.lastJson = JSON.stringify(this.current);
  }

  start(): void {
    if (this.timer !== null) return;
    this.timer = window.setInterval(() => this.poll(), POLL_INTERVAL_MS);
    mediaDevices()?.addEventListener('devicechange', this.onDeviceChange);
    void this.refreshDevices();
  }

  dispose(): void {
    if (this.timer !== null) window.clearInterval(this.timer);
    this.timer = null;
    this.detach();
    mediaDevices()?.removeEventListener('devicechange', this.onDeviceChange);
    this.listeners.clear();
  }

  getStatus(): AudioStatus {
    return this.current;
  }

  subscribe(listener: (status: AudioStatus) => void): () => void {
    this.listeners.add(listener);
    listener(this.current);
    return () => this.listeners.delete(listener);
  }

  /** The browser could not create an AudioContext at all (before any backend exists). */
  noteDeviceError(message: string): void {
    this.deviceError = message;
    this.poll();
  }

  /** A backend failed to start (null clears it, e.g. when Play succeeds on the next try). */
  noteStartError(message: string | null): void {
    this.startError = message;
    if (message === null) this.deviceError = null;
    this.poll();
  }

  /** Re-reads the context and republishes the status when it changed. `nowMs` is for tests. */
  poll(nowMs: number = performance.now()): AudioStatus {
    this.syncContext();
    this.measureClock(nowMs);
    const next = evaluateAudioStatus(this.collect());
    const json = JSON.stringify(next);
    this.current = next;
    if (json !== this.lastJson) {
      this.lastJson = json;
      for (const listener of this.listeners) listener(next);
    }
    return next;
  }

  async refreshDevices(): Promise<void> {
    let count: number | null = null;
    if (browserListsOutputDevices()) {
      try {
        const devices = (await mediaDevices()?.enumerateDevices()) ?? [];
        count = devices.filter((device) => device.kind === 'audiooutput').length;
      } catch {
        count = null;
      }
    }
    this.outputDevices = count;
    this.poll();
  }

  private collect(): AudioStatusInput {
    const ctx = this.context;
    return {
      backend: this.options.getBackend(),
      playRequested: this.options.getPlayRequested(),
      startError: this.startError,
      deviceError: this.deviceError,
      contextState: ctx ? (ctx.state as AudioContextStateLike) : null,
      clockAdvancing: this.clockAdvancing,
      outputDevices: this.outputDevices,
      sampleRate: ctx ? ctx.sampleRate : null,
      latencyMs: ctx ? ((ctx.baseLatency || 0) + (ctx.outputLatency || 0)) * 1000 : null,
    };
  }

  private syncContext(): void {
    const ctx = this.options.getContext();
    if (ctx === this.context) return;
    this.detach();
    this.context = ctx;
    this.clockSample = null;
    this.clockAdvancing = null;
    this.deviceError = null;
    if (ctx) {
      ctx.addEventListener('statechange', this.onContextEvent);
      ctx.addEventListener('sinkchange', this.onContextEvent);
      ctx.addEventListener('error', this.onContextError);
    }
  }

  private detach(): void {
    const ctx = this.context;
    if (!ctx) return;
    ctx.removeEventListener('statechange', this.onContextEvent);
    ctx.removeEventListener('sinkchange', this.onContextEvent);
    ctx.removeEventListener('error', this.onContextError);
    this.context = null;
  }

  /** A running context whose clock stands still has no device pulling samples. */
  private measureClock(nowMs: number): void {
    const ctx = this.context;
    if (!ctx || ctx.state !== 'running') {
      this.clockSample = null;
      this.clockAdvancing = null;
      return;
    }
    const sample = this.clockSample;
    if (!sample) {
      this.clockSample = { wall: nowMs, audio: ctx.currentTime };
      return;
    }
    const wallMs = nowMs - sample.wall;
    if (wallMs < CLOCK_WINDOW_MS) return;
    const audioMs = (ctx.currentTime - sample.audio) * 1000;
    this.clockAdvancing = audioMs > wallMs * CLOCK_MIN_RATE;
    this.clockSample = { wall: nowMs, audio: ctx.currentTime };
  }
}
