/**
 * Script module sandbox (?script=1).
 *
 * User JavaScript runs in a dedicated worker built from WORKER_BOOTSTRAP. Before
 * any user code runs, the bootstrap captures what it needs (postMessage, timers,
 * Atomics) and overwrites network, storage, and worker-spawning globals on every
 * object in the global prototype chain. The script receives only a frozen `bespoke`
 * API; every call becomes a message the main thread validates (scriptHost.ts), except
 * notes while the AudioWorklet engine plays: the bootstrap then writes them into the
 * module's own channel (ScriptChannel in src/audio/engineShared.ts), which the engine
 * processor validates, and keeps time from the engine's clock instead of the main
 * thread's ticks. User code never sees the channel. The worker never sees the WASM
 * heap, Emscripten FS, or the DOM. See docs/wasm/python.md (security review) and
 * docs/wasm/scripting.md.
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

/** A ScriptChannel's buffers as posted to the worker. */
export interface ScriptChannelTransfer {
  ring: SharedArrayBuffer;
  clock: SharedArrayBuffer;
}

export type HostToWorkerMessage =
  | {
      type: 'run';
      code: string;
      modules: Array<{ id: number; type: string }>;
      scriptModuleId: number;
      channel: ScriptChannelTransfer | null;
    }
  | { type: 'tick'; beat: number; bpm: number; playing: boolean }
  | { type: 'ping'; id: number }
  | { type: 'control'; req: number; value: number | null }
  /** The engine is up: notes go to it directly and time comes from its clock. */
  | { type: 'attach'; channel: ScriptChannelTransfer }
  /** The engine is gone: back to messages and the host's ticks. */
  | { type: 'detach' };

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
  // Captured before user code can replace them (the direct channel needs them to stay honest).
  const atomicLoad = Atomics.load;
  const atomicStore = Atomics.store;
  const atomicAdd = Atomics.add;
  const SAB = typeof SharedArrayBuffer === 'function' ? SharedArrayBuffer : null;
  const I32 = Int32Array;
  const F32 = Float32Array;
  const F64 = Float64Array;
  const { floor, round, max, min } = Math;
  const BLOCKED = __BLOCKED__;
  const MAX_TIMERS = 64;
  const MAX_SCHEDULED = 256;
  const MAX_BEAT_HANDLERS = 16;
  const MAX_PENDING_OFFS = 256;
  const MAX_LOG_CHARS = __MAX_LOG__;
  // Direct mode: beats fire this far ahead of the engine, checked this often.
  const LOOKAHEAD_S = 0.03;
  const DIRECT_TICK_MS = 5;

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

  // --- Direct channel to the AudioWorklet engine (ScriptChannel, src/audio/engineShared.ts) ---
  const direct = {
    on: false, header: null, words: null, capacity: 0, clockI: null, clockD: null,
    timer: null, frameEnd: 0, rate: 0, lastBeat: 0,
  };
  /** Start frame of the beat or scheduled call running now; 0 outside them (play at once). */
  let noteFrame = 0;
  /** Its frame on the beat grid, even when it fired too late to start there (durations count from it). */
  let noteAnchor = 0;
  /** Note-offs of noteOn(..., durationMs), written once their frame is inside the lookahead. */
  const pendingOffs = [];
  const velocityBits = new F32(1);
  const velocityWord = new I32(velocityBits.buffer);

  const writeNote = (pitch, velocity, on, frame) => {
    const { header, words, capacity } = direct;
    const write = atomicLoad(header, 0);
    let used = write - atomicLoad(header, 1);
    if (used < 0) used += capacity;
    if (write < 0 || write >= capacity || write % 4 !== 0 || capacity - 4 - used < 4) {
      atomicAdd(header, 3, 1);
      return;
    }
    velocityBits[0] = on ? velocity : 0;
    words[write] = pitch | (on ? 128 : 0) | (frame > 0 ? 256 : 0);
    words[write + 1] = velocityWord[0];
    words[write + 2] = frame > 0 ? frame : 0; // Int32Array keeps the low 32 bits
    words[write + 3] = 0;
    atomicStore(header, 0, write + 4 >= capacity ? 0 : write + 4);
  };

  const readClock = () => {
    const seq = atomicLoad(direct.clockI, 0);
    if (seq === 0 || (seq & 1) !== 0) return null;
    const d = direct.clockD;
    const clock = { beat: d[0], bpm: d[1], frameEnd: d[2], rate: d[3], playing: atomicLoad(direct.clockI, 1) === 1 };
    return atomicLoad(direct.clockI, 0) === seq ? clock : null;
  };

  const detach = () => {
    if (direct.timer !== null) timers.clear(direct.timer);
    Object.assign(direct, { on: false, header: null, words: null, clockI: null, clockD: null, timer: null });
    pendingOffs.length = 0;
  };

  const attach = (channel) => {
    detach();
    if (!SAB || !channel || !(channel.ring instanceof SAB) || !(channel.clock instanceof SAB)) return;
    if (channel.ring.byteLength < 16 + 32 || channel.clock.byteLength < 48) return;
    const words = (channel.ring.byteLength - 16) >> 2;
    direct.capacity = words - (words % 4);
    direct.header = new I32(channel.ring, 0, 4);
    direct.words = new I32(channel.ring, 16, direct.capacity);
    direct.clockI = new I32(channel.clock, 0, 4);
    direct.clockD = new F64(channel.clock, 16, 4);
    direct.on = true;
    direct.lastBeat = 0;
    const clock = readClock();
    if (clock) {
      direct.frameEnd = clock.frameEnd;
      direct.rate = clock.rate;
    }
    direct.timer = timers.set(directTick, DIRECT_TICK_MS);
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
      const message = { type: 'setControl', id, name: String(name), value: Number(value) };
      // Inside a beat that fired ahead of the engine: send it when that beat is about to play.
      const wait = direct.on && noteFrame > 0 && direct.rate > 0 ? ((noteFrame - direct.frameEnd) / direct.rate) * 1000 : 0;
      if (wait >= 1) timers.set(() => post(message), wait);
      else post(message);
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
      if (direct.on) {
        const p = round(pitch);
        if (!(p >= 0 && p <= 127)) return;
        const v = Number(velocity);
        writeNote(p, Number.isFinite(v) ? max(0, min(1, v)) : 0.8, true, noteFrame);
        if (durationMs === undefined) return;
        if (direct.rate > 0) {
          if (pendingOffs.length >= MAX_PENDING_OFFS) throw new RangeError('too many pending note-offs');
          const start = noteAnchor || direct.frameEnd;
          pendingOffs.push({ frame: start + (max(0, Number(durationMs) || 0) * direct.rate) / 1000, pitch: p });
          return;
        }
      } else {
        post({ type: 'note', pitch: Math.round(pitch), velocity: Number(velocity), on: true });
      }
      if (durationMs !== undefined) bespoke.setTimeout(() => bespoke.noteOff(pitch), durationMs);
    },
    noteOff(pitch) {
      if (direct.on) {
        const p = round(pitch);
        if (p >= 0 && p <= 127) writeNote(p, 0, false, noteFrame);
        return;
      }
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

  /**
   * Direct mode's tick: fires everything due before the lookahead horizon in frame order (a
   * note-off before a note-on at the same frame), each with noteFrame set to its own frame.
   */
  const directAdvance = (clock) => {
    const wasPlaying = state.playing;
    state.bpm = clock.bpm;
    state.playing = clock.playing;
    const framesPerBeat = (clock.rate * 60) / clock.bpm;
    const horizonFrame = clock.frameEnd + LOOKAHEAD_S * clock.rate;
    const horizonBeat = clock.beat + (horizonFrame - clock.frameEnd) / framesPerBeat;
    const frameOfBeat = (beat) => clock.frameEnd + (beat - clock.beat) * framesPerBeat;
    // A frame the engine already rendered plays at once rather than counting as late.
    const startOf = (frame) => (frame >= clock.frameEnd ? round(frame) : 0);
    if (!clock.playing) {
      state.beat = 0;
      state.prevBeat = -1;
    } else if (!wasPlaying || clock.beat < direct.lastBeat) {
      state.prevBeat = -1e-9; // (re)start, or the transport jumped back: beat 0 fires again
    } else if (frameOfBeat(floor(state.prevBeat) + 1) < clock.frameEnd - clock.rate) {
      state.prevBeat = floor(clock.beat); // a stall longer than a second: skip, don't burst
    }
    direct.lastBeat = clock.beat;
    for (let guard = 0; guard < 512; ++guard) {
      let kind = 0;
      let at = Infinity;
      let index = -1;
      let beat = 0;
      for (let i = 0; i < pendingOffs.length; ++i) {
        if (pendingOffs[i].frame < at) {
          at = pendingOffs[i].frame;
          kind = 1;
          index = i;
        }
      }
      if (clock.playing) {
        const next = floor(state.prevBeat) + 1;
        if (frameOfBeat(next) < at) {
          at = frameOfBeat(next);
          kind = 2;
          beat = next;
        }
        for (let i = 0; i < scheduled.length; ++i) {
          const frame = frameOfBeat(scheduled[i].beat);
          if (frame < at) {
            at = frame;
            kind = 3;
            index = i;
            beat = scheduled[i].beat;
          }
        }
      }
      if (kind === 0 || at > horizonFrame) break;
      if (kind === 1) {
        const [off] = pendingOffs.splice(index, 1);
        writeNote(off.pitch, 0, false, startOf(off.frame));
        continue;
      }
      state.beat = beat;
      noteFrame = startOf(at);
      noteAnchor = round(at);
      if (kind === 2) {
        state.prevBeat = beat;
        for (const handler of beatHandlers) guarded(handler, beat);
      } else {
        const [event] = scheduled.splice(index, 1);
        guarded(event.fn);
      }
      noteFrame = 0;
      noteAnchor = 0;
    }
    state.beat = clock.playing ? max(0, horizonBeat) : 0;
  };

  const directTick = () => {
    direct.timer = null;
    if (!direct.on) return;
    const clock = readClock();
    if (clock && clock.rate > 0 && clock.bpm > 0) {
      direct.frameEnd = clock.frameEnd;
      direct.rate = clock.rate;
      directAdvance(clock);
    }
    if (direct.on) direct.timer = timers.set(directTick, DIRECT_TICK_MS);
  };

  listen('message', (event) => {
    const msg = event.data || {};
    switch (msg.type) {
      case 'ping':
        post({ type: 'pong', id: msg.id });
        break;
      case 'tick':
        if (!direct.on) onTick(msg);
        break;
      case 'attach':
        attach(msg.channel);
        break;
      case 'detach':
        detach();
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
        if (msg.channel) attach(msg.channel);
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
