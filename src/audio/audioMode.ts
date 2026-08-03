export type AudioBackendId = 'sdl' | 'worklet';

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

export function resolveAudioBackend(search: string = window.location.search): AudioBackendId {
  const params = new URLSearchParams(search);
  const explicit = params.get('audio')?.toLowerCase();
  if (explicit === 'worklet' || explicit === 'sab') return 'worklet';
  if (explicit === 'sdl' || explicit === 'sdl2') return 'sdl';
  return getStoredAudioPreference() ?? 'sdl';
}

export function isAudioWorkletPocRequested(search: string = window.location.search): boolean {
  const params = new URLSearchParams(search);
  return params.get('audioWorkletPoc') === '1' || params.has('audioWorkletPoc');
}

export function isDebugRequested(search: string = window.location.search): boolean {
  const params = new URLSearchParams(search);
  return params.get('debug') === '1' || params.has('debug');
}

export function crossOriginIsolatedOk(): boolean {
  return typeof crossOriginIsolated !== 'undefined' && crossOriginIsolated === true;
}

export function sharedArrayBufferAvailable(): boolean {
  return typeof SharedArrayBuffer !== 'undefined';
}

export function switchAudioPreference(backend: AudioBackendId): void {
  setStoredAudioPreference(backend);
  const url = new URL(window.location.href);
  url.searchParams.set('audio', backend);
  window.location.assign(url.toString());
}
