export type AudioBackendId = 'sdl' | 'worklet';

/** Where the requested backend came from. */
export type AudioBackendSource = 'url' | 'stored' | 'auto';

export interface AudioBackendChoice {
  /** Backend that will actually play. */
  backend: AudioBackendId;
  /** Backend asked for (URL, stored preference, or the auto default). */
  requested: AudioBackendId;
  source: AudioBackendSource;
  /** Why the worklet is not in use when it was requested or would be the default. */
  fallbackReason?: string;
}

const STORAGE_KEY = 'bespokesynth.audio';

export function getStoredAudioPreference(): AudioBackendId | null {
  try {
    const value = window.localStorage.getItem(STORAGE_KEY);
    return value === 'sdl' || value === 'worklet' ? value : null;
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

/** Whether the SAB-ring AudioWorklet backend can run in this page. */
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

function parseAudioParam(search: string): AudioBackendId | null {
  const explicit = new URLSearchParams(search).get('audio')?.toLowerCase();
  if (explicit === 'worklet' || explicit === 'sab') return 'worklet';
  if (explicit === 'sdl' || explicit === 'sdl2') return 'sdl';
  return null;
}

/**
 * Precedence: `?audio=` > stored preference > auto. Auto picks the worklet
 * whenever the page is cross-origin isolated, else SDL2. A worklet request
 * that cannot run resolves to SDL2 with `fallbackReason` set.
 */
export function resolveAudioBackendChoice(search: string = window.location.search): AudioBackendChoice {
  const fromUrl = parseAudioParam(search);
  const stored = fromUrl ? null : getStoredAudioPreference();
  const source: AudioBackendSource = fromUrl ? 'url' : stored ? 'stored' : 'auto';
  const requested: AudioBackendId = fromUrl ?? stored ?? 'worklet';

  if (requested === 'sdl') return { backend: 'sdl', requested, source };

  const support = workletSupport();
  if (support.ok) return { backend: 'worklet', requested, source };
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
