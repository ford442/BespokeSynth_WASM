import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { readAudioHealth, backendName } from '../audio/audioHealth';

export function setupAudioHealthHud(getModule: () => BespokeSynthModule | null): { stop: () => void } {
  let hud = document.getElementById('audioHealthHud');
  if (!hud) {
    hud = document.createElement('div');
    hud.id = 'audioHealthHud';
    hud.className = 'audio-health-hud';
    document.body.appendChild(hud);
  }

  const timer = window.setInterval(() => {
    const health = readAudioHealth(getModule());
    if (!health || !hud) return;
    hud.textContent =
      `audio:${backendName(health.backend)} cpu:${(health.cpuLoad * 100).toFixed(0)}% ` +
      `cb:${health.callbackCount} xrun:${health.underrunCount} ` +
      `q:${health.queueDepthFrames}/${health.capacityFrames} ` +
      `maxP:${health.maxProcessTimeMs.toFixed(2)}ms`;
  }, 250);

  return {
    stop: () => clearInterval(timer),
  };
}
