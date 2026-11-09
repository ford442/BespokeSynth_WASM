/**
 * - `worklet`: the graph runs in a second WASM instance inside the AudioWorklet (Option A, default).
 * - `ring`: the main-thread graph fills a SharedArrayBuffer ring the worklet plays (Option B).
 * - `port`: the main-thread graph posts blocks to an AudioWorklet over its MessagePort (Option C).
 *   Needs no SharedArrayBuffer, so it is what a page without COOP/COEP gets.
 * - `sdl`: Emscripten's SDL2 audio (a deprecated ScriptProcessorNode on the main thread). Only
 *   used when AudioWorklet itself is missing, or on request (`?audio=sdl`).
 */
export type AudioBackendId = 'sdl' | 'worklet' | 'ring' | 'port';

/** Where the requested backend came from. */
export type AudioBackendSource = 'url' | 'stored' | 'auto';

export interface AudioBackendChoice {
  /** Backend that will actually play. */
  backend: AudioBackendId;
  /** Backend asked for (URL, stored preference, or the auto default). */
  requested: AudioBackendId;
  source: AudioBackendSource;
  /** Why the requested backend is not in use (a lesser one plays instead). */
  fallbackReason?: string;
}

const STORAGE_KEY = 'bespokesynth.audio';

export function getStoredAudioPreference(): AudioBackendId | null {
  try {
    const value = window.localStorage.getItem(STORAGE_KEY);
    return value === 'sdl' || value === 'worklet' || value === 'ring' || value === 'port' ? value : null;
  } catch {
    return null;
  }
}

export function setStoredAudioPreference(backend: AudioBackendId): void {
  try {
    window.localStorage.setItem(STORAGE_KEY, backend);
  } catch {
    // ignore
  }
}

export function crossOriginIsolatedOk(): boolean {
  return typeof crossOriginIsolated !== 'undefined' && crossOriginIsolated === true;
}

export function sharedArrayBufferAvailable(): boolean {
  return typeof SharedArrayBuffer !== 'undefined';
}

/** Whether the AudioWorklet backends (engine and SAB ring both use SharedArrayBuffer) can run. */
export function workletSupport(): { ok: boolean; reason?: string } {
  if (typeof AudioWorkletNode === 'undefined') {
    return { ok: false, reason: 'AudioWorkletNode unavailable' };
  }
  if (!crossOriginIsolatedOk()) {
    return {
      ok: false,
      reason: 'page is not cross-origin isolated (server must send COOP/COEP headers)',
    };
  }
  if (!sharedArrayBufferAvailable()) {
    return { ok: false, reason: 'SharedArrayBuffer unavailable' };
  }
  return { ok: true };
}

/** Whether the message-port AudioWorklet player can run: it needs AudioWorklet, not isolation. */
export function portWorkletSupport(): { ok: boolean; reason?: string } {
  if (typeof AudioWorkletNode === 'undefined') {
    return { ok: false, reason: 'AudioWorkletNode unavailable' };
  }
  return { ok: true };
}

function parseAudioParam(search: string): AudioBackendId | null {
  const explicit = new URLSearchParams(search).get('audio')?.toLowerCase();
  if (explicit === 'worklet' || explicit === 'wasm') return 'worklet';
  if (explicit === 'ring' || explicit === 'sab') return 'ring';
  if (explicit === 'port' || explicit === 'message') return 'port';
  if (explicit === 'sdl' || explicit === 'sdl2') return 'sdl';
  return null;
}

/**
 * Precedence: `?audio=` > stored preference > auto. Auto picks the worklet engine whenever the
 * page is cross-origin isolated. A worklet or ring request that cannot run (no isolation) resolves
 * to the message-port AudioWorklet player with `fallbackReason` set; only a browser without
 * AudioWorklet ends on SDL2.
 */
export function resolveAudioBackendChoice(search: string = window.location.search): AudioBackendChoice {
  const fromUrl = parseAudioParam(search);
  const stored = fromUrl ? null : getStoredAudioPreference();
  const source: AudioBackendSource = fromUrl ? 'url' : stored ? 'stored' : 'auto';
  const requested: AudioBackendId = fromUrl ?? stored ?? 'worklet';

  if (requested === 'sdl') return { backend: 'sdl', requested, source };

  const port = portWorkletSupport();
  if (requested === 'port') {
    return port.ok ? { backend: 'port', requested, source } : { backend: 'sdl', requested, source, fallbackReason: port.reason };
  }

  const support = workletSupport();
  if (support.ok) return { backend: requested, requested, source };
  if (port.ok) return { backend: 'port', requested, source, fallbackReason: support.reason };
  return { backend: 'sdl', requested, source, fallbackReason: support.reason };
}

export function resolveAudioBackend(search: string = window.location.search): AudioBackendId {
  return resolveAudioBackendChoice(search).backend;
}

/** Persist an explicit `?audio=` override so later visits without the flag keep it. */
export function persistUrlAudioPreference(search: string = window.location.search): void {
  const fromUrl = parseAudioParam(search);
  if (fromUrl) setStoredAudioPreference(fromUrl);
}

export function isAudioWorkletPocRequested(search: string = window.location.search): boolean {
  const params = new URLSearchParams(search);
  return params.get('audioWorkletPoc') === '1' || params.has('audioWorkletPoc');
}

export function isDebugRequested(search: string = window.location.search): boolean {
  const params = new URLSearchParams(search);
  return params.get('debug') === '1' || params.has('debug');
}

export function switchAudioPreference(backend: AudioBackendId): void {
  setStoredAudioPreference(backend);
  const url = new URL(window.location.href);
  url.searchParams.set('audio', backend);
  window.location.assign(url.toString());
}
