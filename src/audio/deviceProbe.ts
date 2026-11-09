/**
 * Reads the output device's native sample rate before the engine decodes any sample.
 *
 * The sampler does not rate-convert: samples are resampled to the device rate when they load, and
 * the bundled kit loads inside bespoke_init. Nothing opens an audio device at load any more (SDL2
 * waits for Play), so without this the kit would decode at a guessed 44.1 kHz and play out of
 * pitch on the 48 kHz devices most machines have.
 *
 * The probe is one AudioContext that is closed again at once: no node, no ScriptProcessor, no
 * worklet. Its constructor throwing is itself a finding (no audio stack), reported as `error`.
 */

export const DEFAULT_SAMPLE_RATE = 44100;

export interface AudioDeviceProbe {
  sampleRate: number;
  /** Why no AudioContext could be created; null when one could. */
  error: string | null;
}

export function probeAudioDevice(): AudioDeviceProbe {
  if (typeof AudioContext === 'undefined') {
    return { sampleRate: DEFAULT_SAMPLE_RATE, error: 'Web Audio (AudioContext) is not available in this browser' };
  }
  try {
    const context = new AudioContext();
    const rate = context.sampleRate;
    void context.close().catch(() => undefined);
    const sane = Number.isFinite(rate) && rate >= 8000 && rate <= 384000;
    return { sampleRate: sane ? rate : DEFAULT_SAMPLE_RATE, error: null };
  } catch (error) {
    return { sampleRate: DEFAULT_SAMPLE_RATE, error: error instanceof Error ? error.message : String(error) };
  }
}
