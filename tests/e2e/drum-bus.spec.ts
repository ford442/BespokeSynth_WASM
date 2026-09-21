import { test, expect } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

test.describe('Drum bus render test (?renderTest=2)', () => {
  test('loads drumseq → mixer → compressor and plays a loop without MIDI', async ({ page }) => {
    await gotoApp(page, 'renderTest=2');
    await waitForBespokeReady(page);

    const scene = await page.evaluate(() => {
      const api = (window as Window & {
        __bespoke?: {
          getRenderTestScene?: () => number;
          getModuleCount?: () => number;
          getStateJson?: () => string | null;
        };
      }).__bespoke;
      const stateJson = api?.getStateJson?.() ?? '';
      return {
        scene: api?.getRenderTestScene?.() ?? 0,
        count: api?.getModuleCount?.() ?? 0,
        types: ['drumseq', 'mixer', 'compressor', 'adsr', 'noise'].filter((t) => stateJson.includes(`"${t}"`)),
      };
    });

    expect(scene.scene).toBe(2);
    expect(scene.types).toEqual(['drumseq', 'mixer', 'compressor', 'adsr', 'noise']);
    // transport + scale + drumseq + noise + kick osc + 2 adsr + mixer + compressor + output
    expect(scene.count).toBeGreaterThanOrEqual(10);

    // Offline render forces the transport on: Pulse → DrumSeq must make sound by itself.
    const level = await page.evaluate(() => {
      const api = (window as Window & {
        __bespoke?: { renderOfflineWav?: (opts?: { seconds?: number; bitsPerSample?: 16 }) => Uint8Array | null };
      }).__bespoke;
      const wav = api?.renderOfflineWav?.({ seconds: 2, bitsPerSample: 16 });
      if (!wav) return null;
      const view = new DataView(wav.buffer, wav.byteOffset, wav.byteLength);
      let offset = 12;
      while (offset + 8 <= view.byteLength) {
        const id = String.fromCharCode(
          view.getUint8(offset),
          view.getUint8(offset + 1),
          view.getUint8(offset + 2),
          view.getUint8(offset + 3),
        );
        const size = view.getUint32(offset + 4, true);
        if (id === 'data') {
          let sum = 0;
          let peak = 0;
          const samples = Math.floor(size / 2);
          for (let i = 0; i < samples; i++) {
            const v = view.getInt16(offset + 8 + i * 2, true) / 32768;
            sum += v * v;
            peak = Math.max(peak, Math.abs(v));
          }
          return { rms: Math.sqrt(sum / Math.max(1, samples)), peak };
        }
        offset += 8 + size + (size % 2);
      }
      return null;
    });

    expect(level).toBeTruthy();
    expect(level!.rms).toBeGreaterThan(0.01);
    expect(level!.peak).toBeLessThanOrEqual(1);
  });
});
