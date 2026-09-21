/**
 * Script module sandbox (?script=1).
 *
 * User JavaScript runs in a dedicated worker built from WORKER_BOOTSTRAP. Before
 * any user code runs, the bootstrap captures what it needs (postMessage, timers)
 * and overwrites network, storage, and worker-spawning globals on every object in
 * the global prototype chain. The script receives only a frozen `bespoke` API;
 * every call becomes a message the main thread validates (scriptHost.ts). The
 * worker never sees the WASM heap, Emscripten FS, or the DOM. See
 * docs/wasm/python.md (security review) and docs/wasm/scripting.md.
 */

export const MAX_SCRIPT_BYTES = 64 * 1024;
export const MAX_LOG_CHARS = 500;
export const MAX_CONTROL_NAME = 64;

/** Globals removed from the worker before user code runs. */
export const BLOCKED_WORKER_GLOBALS = Object.freeze([
  'fetch',
  'XMLHttpRequest',
  'WebSocket',
  'WebTransport',
  'EventSource',
  'importScripts',
  'indexedDB',
  'caches',
  'Worker',
  'SharedWorker',
  'BroadcastChannel',
  'MessageChannel',
  'RTCPeerConnection',
  'Request',
  'Response',
  'Headers',
  'navigator',
  'WebAssembly',
  'FileReader',
  'FileReaderSync',
  'Notification',
  'postMessage',
  'setTimeout',
  'setInterval',
  'clearTimeout',
  'clearInterval',
]);

/**
 * Static checks before code reaches a worker. `import()` would reach the network
 * without any global, so the keyword is refused outright.
 */
export function validateScriptSource(code: string): string | null {
  if (typeof code !== 'string') return 'Script must be text';
  if (new TextEncoder().encode(code).byteLength > MAX_SCRIPT_BYTES) {
    return `Script is larger than ${MAX_SCRIPT_BYTES / 1024} KiB`;
  }
  if (/\bimport\b/.test(code)) return '`import` is not available in scripts (no network or modules)';
  return null;
}

/** Messages the worker may send; anything else is dropped by the host. */
export type WorkerToHostMessage =
  | { type: 'ready' }
  | { type: 'pong'; id: number }
  | { type: 'log'; level: 'log' | 'error'; text: string }
  | { type: 'setControl'; id: number; name: string; value: number }
  | { type: 'getControl'; req: number; id: number; name: string }
  | { type: 'note'; pitch: number; velocity: number; on: boolean };

export type HostToWorkerMessage =
  | { type: 'run'; code: string; modules: Array<{ id: number; type: string }>; scriptModuleId: number }
  | { type: 'tick'; beat: number; bpm: number; playing: boolean }
  | { type: 'ping'; id: number }
  | { type: 'control'; req: number; value: number | null };

/** Returns a sanitized message, or null when the worker sent something invalid. */
export function sanitizeWorkerMessage(raw: unknown): WorkerToHostMessage | null {
  if (!raw || typeof raw !== 'object') return null;
  const msg = raw as Record<string, unknown>;
  const finite = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
  const shortName = (v: unknown): v is string =>
    typeof v === 'string' && v.length > 0 && v.length <= MAX_CONTROL_NAME && /^[A-Za-z0-9_]+$/.test(v);
  switch (msg.type) {
    case 'ready':
      return { type: 'ready' };
    case 'pong':
      return finite(msg.id) ? { type: 'pong', id: msg.id } : null;
    case 'log':
      return {
        type: 'log',
        level: msg.level === 'error' ? 'error' : 'log',
        text: String(msg.text ?? '').slice(0, MAX_LOG_CHARS),
      };
    case 'setControl':
      return Number.isInteger(msg.id) && shortName(msg.name) && finite(msg.value)
        ? { type: 'setControl', id: msg.id as number, name: msg.name, value: msg.value }
        : null;
    case 'getControl':
      return Number.isInteger(msg.req) && Number.isInteger(msg.id) && shortName(msg.name)
        ? { type: 'getControl', req: msg.req as number, id: msg.id as number, name: msg.name }
        : null;
    case 'note': {
      const pitch = msg.pitch;
      if (!Number.isInteger(pitch) || (pitch as number) < 0 || (pitch as number) > 127) return null;
      const velocity = finite(msg.velocity) ? Math.max(0, Math.min(1, msg.velocity)) : 0.8;
      return { type: 'note', pitch: pitch as number, velocity, on: msg.on === true };
    }
    default:
      return null;
  }
}

/**
 * Worker source. Plain JS kept as a string so the worker can be created from a
 * Blob URL (same origin, COEP-compatible) without a second bundle entry.
 */
export const WORKER_BOOTSTRAP = String.raw`'use strict';
(() => {
  const post = self.postMessage.bind(self);
  const listen = self.addEventListener.bind(self);
  const timers = { set: self.setTimeout.bind(self), clear: self.clearTimeout.bind(self) };
  const BLOCKED = __BLOCKED__;
  const MAX_TIMERS = 64;
  const MAX_SCHEDULED = 256;
  const MAX_BEAT_HANDLERS = 16;
  const MAX_LOG_CHARS = __MAX_LOG__;

  // Remove capabilities everywhere they can be looked up, then freeze.
  for (let scope = self; scope && scope !== Object.prototype; scope = Object.getPrototypeOf(scope)) {
    for (const name of BLOCKED) {
      if (!Object.prototype.hasOwnProperty.call(scope, name)) continue;
      try {
        Object.defineProperty(scope, name, { value: undefined, writable: false, configurable: false });
      } catch (error) {
        try { delete scope[name]; } catch (ignored) { /* best effort */ }
      }
    }
  }

  const state = { beat: 0, bpm: 120, playing: false, modules: [], scriptModuleId: -1, prevBeat: -1 };
  const beatHandlers = [];
  const scheduled = [];
  const liveTimers = new Set();
  const pendingControls = new Map();
  let nextRequest = 1;

  const format = (args) => args.map((a) => {
    if (typeof a === 'string') return a;
    try { return JSON.stringify(a); } catch (error) { return String(a); }
  }).join(' ').slice(0, MAX_LOG_CHARS);

  const report = (error) => {
    const text = error && error.stack ? String(error.stack) : String(error);
    post({ type: 'log', level: 'error', text: text.slice(0, MAX_LOG_CHARS) });
  };

  const guarded = (fn, ...args) => {
    try { fn(...args); } catch (error) { report(error); }
  };

  const checkId = (id) => {
    if (!Number.isInteger(id) || id < 0) throw new TypeError('module id must be a non-negative integer');
  };

  const bespoke = Object.freeze({
    get transport() {
      const beat = state.beat;
      return Object.freeze({
        beat,
        bar: Math.floor(beat / 4),
        beatInBar: Math.floor(beat) % 4,
        bpm: state.bpm,
        playing: state.playing,
      });
    },
    get scriptModuleId() { return state.scriptModuleId; },
    modules() { return state.modules.map((m) => Object.freeze({ id: m.id, type: m.type })); },
    findModule(type) {
      const found = state.modules.find((m) => m.type === type);
      return found ? found.id : -1;
    },
    setControl(id, name, value) {
      checkId(id);
      post({ type: 'setControl', id, name: String(name), value: Number(value) });
    },
    getControl(id, name) {
      checkId(id);
      const req = nextRequest++;
      return new Promise((resolve) => {
        pendingControls.set(req, resolve);
        post({ type: 'getControl', req, id, name: String(name) });
      });
    },
    noteOn(pitch, velocity = 0.8, durationMs) {
      post({ type: 'note', pitch: Math.round(pitch), velocity: Number(velocity), on: true });
      if (durationMs !== undefined) bespoke.setTimeout(() => bespoke.noteOff(pitch), durationMs);
    },
    noteOff(pitch) {
      post({ type: 'note', pitch: Math.round(pitch), velocity: 0, on: false });
    },
    /** fn(beatIndex) on every beat while playing; beat 0 is bar 1, beat 1. */
    onBeat(fn) {
      if (typeof fn !== 'function') throw new TypeError('onBeat needs a function');
      if (beatHandlers.length >= MAX_BEAT_HANDLERS) throw new RangeError('too many onBeat handlers');
      beatHandlers.push(fn);
    },
    /** Run fn once the transport reaches the current beat + beatsFromNow. */
    schedule(beatsFromNow, fn) {
      if (typeof fn !== 'function') throw new TypeError('schedule needs a function');
      if (scheduled.length >= MAX_SCHEDULED) throw new RangeError('too many scheduled events');
      scheduled.push({ beat: state.beat + Math.max(0, Number(beatsFromNow) || 0), fn });
    },
    setTimeout(fn, ms) {
      if (typeof fn !== 'function') throw new TypeError('setTimeout needs a function');
      if (liveTimers.size >= MAX_TIMERS) throw new RangeError('too many timers');
      const delay = Math.min(60000, Math.max(5, Number(ms) || 0));
      const handle = timers.set(() => {
        liveTimers.delete(handle);
        guarded(fn);
      }, delay);
      liveTimers.add(handle);
      return handle;
    },
    clearTimeout(handle) {
      if (liveTimers.delete(handle)) timers.clear(handle);
    },
    log(...args) { post({ type: 'log', level: 'log', text: format(args) }); },
  });

  const onTick = (tick) => {
    const wasPlaying = state.playing;
    state.bpm = tick.bpm;
    state.playing = tick.playing;
    if (!tick.playing) {
      state.beat = 0;
      state.prevBeat = -1;
      return;
    }
    // (Re)start, or the transport jumped backwards: beat 0 fires again.
    if (!wasPlaying || tick.beat < state.prevBeat) state.prevBeat = -1e-9;
    state.beat = tick.beat;
    const from = Math.floor(state.prevBeat);
    const to = Math.floor(tick.beat);
    for (let beat = from + 1; beat <= to && beat - from <= 64; ++beat) {
      for (const handler of beatHandlers) guarded(handler, beat);
    }
    state.prevBeat = tick.beat;
    for (let i = scheduled.length - 1; i >= 0; --i) {
      if (scheduled[i].beat <= tick.beat) {
        const [event] = scheduled.splice(i, 1);
        guarded(event.fn);
      }
    }
  };

  listen('message', (event) => {
    const msg = event.data || {};
    switch (msg.type) {
      case 'ping':
        post({ type: 'pong', id: msg.id });
        break;
      case 'tick':
        onTick(msg);
        break;
      case 'control': {
        const resolve = pendingControls.get(msg.req);
        pendingControls.delete(msg.req);
        if (resolve) resolve(msg.value);
        break;
      }
      case 'run': {
        state.modules = Array.isArray(msg.modules) ? msg.modules : [];
        state.scriptModuleId = msg.scriptModuleId;
        if (/\bimport\b/.test(msg.code)) {
          report(new Error('import is not available in scripts'));
          break;
        }
        try {
          const body = new Function('bespoke', '"use strict";\n' + msg.code);
          body(bespoke);
        } catch (error) {
          report(error);
        }
        break;
      }
      default:
        break;
    }
  });

  self.addEventListener('error', (event) => report(event.error || event.message));
  self.addEventListener('unhandledrejection', (event) => report(event.reason));
  post({ type: 'ready' });
})();
`
  .replace('__BLOCKED__', JSON.stringify(BLOCKED_WORKER_GLOBALS))
  .replace('__MAX_LOG__', String(MAX_LOG_CHARS));

/** Starter script shown in an empty Script panel. */
export const DEFAULT_SCRIPT = `// Runs in a sandboxed worker: no fetch, no DOM, no WASM heap.
// API: bespoke.onBeat(fn), schedule(beats, fn), setControl(id, name, v),
//      getControl(id, name) -> Promise, noteOn(pitch, vel, ms), noteOff(pitch),
//      findModule(type), modules(), transport, setTimeout(fn, ms), log(...)
const osc = bespoke.findModule('oscillator');
bespoke.onBeat((beat) => {
  // Beat 1 of every bar: jump the oscillator to A4; other beats sit an octave down.
  bespoke.setControl(osc, 'frequency', beat % 4 === 0 ? 440 : 220);
  if (beat % 4 === 0) bespoke.log('bar', beat / 4 + 1);
});
`;
