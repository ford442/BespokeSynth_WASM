import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { toArrayBuffer } from './bytes';

export interface OfflineRenderOptions {
  seconds?: number;
  sampleRate?: number;
  bitsPerSample?: 16 | 24 | 32;
  filename?: string;
}

export function renderOfflineWav(module: BespokeSynthModule, options: OfflineRenderOptions = {}): Uint8Array | null {
  const seconds = options.seconds ?? 4;
  const sampleRate = options.sampleRate ?? 44100;
  if (options.bitsPerSample) {
    module._bespoke_set_offline_format(options.bitsPerSample);
  }
  if (module._bespoke_render_offline(seconds, sampleRate) !== 1) {
    return null;
  }
  const length = module._bespoke_get_offline_wav_size();
  const dataPtr = module._bespoke_get_offline_wav(0);
  if (!dataPtr || length <= 0) return null;
  const copy = module.HEAPU8.slice(dataPtr, dataPtr + length);
  module._bespoke_free_offline_render();
  return copy;
}

export function downloadOfflineWav(module: BespokeSynthModule, options: OfflineRenderOptions = {}): boolean {
  const bytes = renderOfflineWav(module, options);
  if (!bytes) return false;
  const blob = new Blob([toArrayBuffer(bytes)], { type: 'audio/wav' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = options.filename ?? 'bespokesynth-export.wav';
  link.click();
  URL.revokeObjectURL(url);
  return true;
}
