import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import type { ScriptChannel, ScriptEngineLink } from '../audio/engineShared';
import { readJson, withCStrings } from '../app/wasmStrings';
import {
  WORKER_BOOTSTRAP,
  sanitizeWorkerMessage,
  validateScriptSource,
  type HostToWorkerMessage,
  type ScriptChannelTransfer,
} from './scriptSandbox';

export type ScriptStatus = 'stopped' | 'running' | 'error' | 'killed';

export type ScriptNoteRoute = 0 | 1 | 2;

export interface ScriptLogEntry {
  level: 'log' | 'error' | 'system';
  text: string;
}

export interface ScriptHostCallbacks {
  onLog?: (entry: ScriptLogEntry) => void;
  onStatus?: (status: ScriptStatus, detail?: string) => void;
  /**
   * Mirror script notes elsewhere (e.g. the selected MIDI output). `route` is what the engine did with the
   * note: 0 rejected, 1 queued on the script module's own Note cables (the engine reports those to MIDI
   * out itself), 2 played like a keyboard because the module has no cable (the host echoes those).
   * Not called for notes a worker sends straight to the AudioWorklet engine: it reports both kinds.
   */
  onNote?: (pitch: number, velocity: number, on: boolean, route: ScriptNoteRoute) => void;
}

const TICK_MS = 20;
const PING_MS = 500;
/** A worker that misses pongs this long is wedged (e.g. `while (true) {}`). */
export const HANG_TIMEOUT_MS = 2000;
const MAX_RESTARTS = 2;
/** Per-window message budget; floods are dropped, not queued. */
const RATE_WINDOW_MS = 250;
const RATE_MAX_MESSAGES = 400;

/**
 * Main-thread side of a Script module. Owns the worker lifecycle (create, feed
 * transport ticks, watchdog, terminate/recreate) and turns validated worker
 * messages into WASM calls that never touch the audio callback directly.
 *
 * While the AudioWorklet engine plays, the worker instead gets its own channel to it
 * (ScriptEngineLink): notes go straight into the engine with their start frame and
 * beats are timed from the engine's clock, so a busy main thread delays neither.
 * Controls, logs and the watchdog still go through here.
 */
export class ScriptHost {
  private worker: Worker | null = null;
  private workerUrl: string | null = null;
  private tickTimer: number | null = null;
  private pingTimer: number | null = null;
  private lastPong = 0;
  private pingId = 0;
  private restarts = 0;
  private code = '';
  private windowStart = 0;
  private windowCount = 0;
  private droppedWarned = false;
  private readonly heldNotes = new Set<number>();
  private status: ScriptStatus = 'stopped';
  private readonly module: BespokeSynthModule;
  private readonly callbacks: ScriptHostCallbacks;
  private readonly getLink: () => ScriptEngineLink | null;
  /** The engine the worker's channel belongs to; null while notes go through this host. */
  private link: ScriptEngineLink | null = null;
  private channel: ScriptChannel | null = null;
  readonly moduleId: number;

  constructor(
    module: BespokeSynthModule,
    moduleId: number,
    callbacks: ScriptHostCallbacks = {},
    getLink: () => ScriptEngineLink | null = () => null,
  ) {
    this.module = module;
    this.moduleId = moduleId;
    this.callbacks = callbacks;
    this.getLink = getLink;
  }

  /** True while the worker talks to the AudioWorklet engine directly. */
  isDirect(): boolean {
    return this.channel !== null;
  }

  getStatus(): ScriptStatus {
    return this.status;
  }

  /** Explicit user action only: loaded patches never auto-run their script. */
  run(code: string): boolean {
    const problem = validateScriptSource(code);
    if (problem) {
      this.log('error', problem);
      this.setStatus('error', problem);
      return false;
    }
    this.stop();
    this.code = code;
    this.restarts = 0;
    this.spawn();
    return true;
  }

  stop(): void {
    this.teardown();
    this.setStatus('stopped');
  }

  private spawn(): void {
    this.workerUrl = URL.createObjectURL(new Blob([WORKER_BOOTSTRAP], { type: 'text/javascript' }));
    const worker = new Worker(this.workerUrl, { name: `bespoke-script-${this.moduleId}` });
    this.worker = worker;
    this.lastPong = performance.now();
    worker.onmessage = (event: MessageEvent) => this.handleMessage(event.data);
    worker.onerror = (event: ErrorEvent) => {
      event.preventDefault();
      this.log('error', event.message || 'Script worker error');
    };
    const channel = this.openChannel();
    this.post({
      type: 'run',
      code: this.code,
      modules: this.listModules(),
      scriptModuleId: this.moduleId,
      channel: channel ? transferOf(channel) : null,
    });
    this.tickTimer = window.setInterval(() => this.tick(), TICK_MS);
    this.pingTimer = window.setInterval(() => this.watchdog(), PING_MS);
    this.setStatus('running');
  }

  private teardown(): void {
    if (this.tickTimer !== null) window.clearInterval(this.tickTimer);
    if (this.pingTimer !== null) window.clearInterval(this.pingTimer);
    this.tickTimer = null;
    this.pingTimer = null;
    this.worker?.terminate();
    this.worker = null;
    if (this.workerUrl) URL.revokeObjectURL(this.workerUrl);
    this.workerUrl = null;
    this.closeChannel();
    for (const pitch of this.heldNotes) this.sendNote(pitch, 0, false);
    this.heldNotes.clear();
    // Also releases what the worker held through its channel (ScriptClear).
    this.module._bespoke_script_stopped(this.moduleId);
  }

  private openChannel(): ScriptChannel | null {
    const link = this.getLink();
    this.channel = link?.openScriptChannel(this.moduleId) ?? null;
    this.link = this.channel ? link : null;
    return this.channel;
  }

  private closeChannel(): void {
    if (this.channel) this.link?.closeScriptChannel(this.channel);
    this.channel = null;
    this.link = null;
  }

  /** The AudioWorklet engine started or went away: move the running worker onto (or off) its channel. */
  private syncChannel(): void {
    if (this.getLink() === this.link) return;
    const wasDirect = this.channel !== null;
    this.closeChannel();
    const channel = this.openChannel();
    if (channel) this.post({ type: 'attach', channel: transferOf(channel) });
    else if (wasDirect) this.post({ type: 'detach' });
  }

  private watchdog(): void {
    if (!this.moduleExists()) {
      this.log('system', 'Script module was removed; stopping.');
      this.stop();
      return;
    }
    if (performance.now() - this.lastPong > HANG_TIMEOUT_MS) {
      this.teardown();
      if (this.restarts < MAX_RESTARTS) {
        this.restarts++;
        this.log('system', `Script unresponsive for ${HANG_TIMEOUT_MS} ms; restarted worker (${this.restarts}/${MAX_RESTARTS}).`);
        this.spawn();
      } else {
        this.log('error', 'Script kept hanging; worker terminated.');
        this.setStatus('killed', 'unresponsive');
      }
      return;
    }
    this.post({ type: 'ping', id: ++this.pingId });
  }

  private tick(): void {
    this.syncChannel();
    if (this.channel) return; // the worker keeps time from the engine's clock
    this.post({
      type: 'tick',
      beat: this.module._bespoke_get_transport_beat(),
      bpm: this.module._bespoke_get_tempo(),
      playing: this.module._bespoke_is_transport_playing() === 1,
    });
  }

  private handleMessage(raw: unknown): void {
    const now = performance.now();
    if (now - this.windowStart > RATE_WINDOW_MS) {
      this.windowStart = now;
      this.windowCount = 0;
    }
    if (++this.windowCount > RATE_MAX_MESSAGES) {
      if (!this.droppedWarned) {
        this.droppedWarned = true;
        this.log('system', `Script is sending more than ${RATE_MAX_MESSAGES} messages per ${RATE_WINDOW_MS} ms; extra messages dropped.`);
      }
      return;
    }

    const msg = sanitizeWorkerMessage(raw);
    if (!msg) return;
    switch (msg.type) {
      case 'ready':
        break;
      case 'pong':
        this.lastPong = now;
        break;
      case 'log':
        this.log(msg.level, msg.text);
        break;
      case 'setControl':
        withCStrings(this.module, [msg.name], (namePtr) =>
          this.module._bespoke_script_set_control(msg.id, namePtr, msg.value),
        );
        break;
      case 'getControl': {
        const value = withCStrings(this.module, [msg.name], (namePtr) =>
          this.module._bespoke_get_control_value(msg.id, namePtr),
        );
        this.post({ type: 'control', req: msg.req, value: Number.isFinite(value) ? value : null });
        break;
      }
      case 'note':
        this.sendNote(msg.pitch, msg.velocity, msg.on);
        break;
    }
  }

  private sendNote(pitch: number, velocity: number, on: boolean): void {
    if (on) this.heldNotes.add(pitch);
    else this.heldNotes.delete(pitch);
    const route = this.module._bespoke_script_note(this.moduleId, pitch, velocity, on ? 1 : 0) as ScriptNoteRoute;
    this.callbacks.onNote?.(pitch, velocity, on, route);
  }

  private listModules(): Array<{ id: number; type: string }> {
    return readJson(this.module, this.module._bespoke_get_modules_json(), []);
  }

  private moduleExists(): boolean {
    const info = readJson<{ id?: number }>(this.module, this.module._bespoke_get_module_info_json(this.moduleId), {});
    return info.id === this.moduleId;
  }

  private post(message: HostToWorkerMessage): void {
    this.worker?.postMessage(message);
  }

  private log(level: ScriptLogEntry['level'], text: string): void {
    this.callbacks.onLog?.({ level, text });
  }

  private setStatus(status: ScriptStatus, detail?: string): void {
    this.status = status;
    withCStrings(this.module, ['status', status], (key, value) =>
      this.module._bespoke_set_module_string(this.moduleId, key, value),
    );
    this.callbacks.onStatus?.(status, detail);
  }
}

function transferOf(channel: ScriptChannel): ScriptChannelTransfer {
  return { ring: channel.ring.sab, clock: channel.clock.sab };
}
