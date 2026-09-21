import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { readAudioHealth, backendName } from '../audio/audioHealth';
import type { WorkletRingStats } from '../audio/workletRingBackend';

export function setupAudioHealthHud(
  getModule: () => BespokeSynthModule | null,
  getRingStats: () => WorkletRingStats | null = () => null,
): { stop: () => void } {
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
    const ring = getRingStats();
    const heapMb = health.heapBytes ? ` heap:${(health.heapBytes / (1024 * 1024)).toFixed(0)}MB` : '';
    hud.textContent =
      `audio:${backendName(health.backend)} cpu:${(health.cpuLoad * 100).toFixed(0)}% ` +
      `cb:${health.callbackCount} xrun:${health.underrunCount} ` +
      `q:${health.queueDepthFrames}/${health.capacityFrames} ` +
      (ring ? `la:${ring.lookaheadFrames} ` : '') +
      `maxP:${health.maxProcessTimeMs.toFixed(2)}ms` +
      heapMb;
  }, 250);

  return {
    stop: () => clearInterval(timer),
  };
}
