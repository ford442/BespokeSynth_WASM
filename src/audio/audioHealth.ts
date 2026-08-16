import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

export interface AudioHealth {
  backend: number;
  callbackCount: number;
  underrunCount: number;
  externalUnderrunCount: number;
  lastCallbackIntervalMs: number;
  lastProcessTimeMs: number;
  maxProcessTimeMs: number;
  cpuLoad: number;
  queueDepthFrames: number;
  maxQueueDepthFrames: number;
  capacityFrames: number;
  noteDropCount: number;
}

export function readAudioHealth(module: BespokeSynthModule | null): AudioHealth | null {
  if (!module?._bespoke_get_audio_health_json) return null;
  const ptr = module._bespoke_get_audio_health_json();
  if (!ptr) return null;
  try {
    return JSON.parse(module.UTF8ToString(ptr)) as AudioHealth;
  } catch {
    return null;
  }
}

export function backendName(id: number): string {
  switch (id) {
    case 1:
      return 'worklet';
    case 2:
      return 'poc';
    default:
      return 'sdl';
  }
}
