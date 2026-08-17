import { test, expect } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

test.describe('sample I/O and shareable patches', () => {
  test('share URL round-trips a patch in a clean profile', async ({ page, browser }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);

    const url = await page.evaluate(async () => {
      const api = (window as Window & {
        __bespoke?: {
          getStateJson?: () => string;
          sharePatch?: () => Promise<string>;
        };
      }).__bespoke;
      return api?.sharePatch?.() ?? '';
    });

    expect(url).toContain('#p=');

    const context = await browser.newContext();
    const fresh = await context.newPage();
    await fresh.goto(url.includes('renderer=') ? url : `${url}${url.includes('?') ? '&' : '?'}renderer=webgl`);
    await expect(fresh.locator('#canvas')).toBeVisible({ timeout: 5000 });
    await waitForBespokeReady(fresh);

    const same = await fresh.evaluate(() => {
      const api = (window as Window & { __bespoke?: { getStateJson?: () => string } }).__bespoke;
      const json = api?.getStateJson?.() ?? '{}';
      const parsed = JSON.parse(json) as { modules?: unknown[] };
      return (parsed.modules?.length ?? 0) > 0;
    });
    expect(same).toBe(true);
    await context.close();
  });

  test('importing a WAV file assigns it to a sampler', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);

    const result = await page.evaluate(async () => {
      const sampleRate = 44100;
      const frames = 256;
      const dataSize = frames * 2;
      const buffer = new ArrayBuffer(44 + dataSize);
      const view = new DataView(buffer);
      const writeString = (offset: number, text: string) => {
        for (let i = 0; i < text.length; i++) view.setUint8(offset + i, text.charCodeAt(i));
      };
      writeString(0, 'RIFF');
      view.setUint32(4, 36 + dataSize, true);
      writeString(8, 'WAVE');
      writeString(12, 'fmt ');
      view.setUint32(16, 16, true);
      view.setUint16(20, 1, true);
      view.setUint16(22, 1, true);
      view.setUint32(24, sampleRate, true);
      view.setUint32(28, sampleRate * 2, true);
      view.setUint16(32, 2, true);
      view.setUint16(34, 16, true);
      writeString(36, 'data');
      view.setUint32(40, dataSize, true);
      for (let i = 0; i < frames; i++) {
        view.setInt16(44 + i * 2, Math.round(Math.sin((i / frames) * Math.PI * 2) * 16000), true);
      }
      const file = new File([buffer], 'tone.wav', { type: 'audio/wav' });
      const api = (window as Window & {
        __bespoke?: { importSampleFile?: (file: File) => Promise<number> };
      }).__bespoke;
      const sampleId = await api?.importSampleFile?.(file);
      return { sampleId };
    });

    expect(result.sampleId).toBeGreaterThanOrEqual(0);
  });

  test('offline render produces a WAV larger than a header', async ({ page }) => {
    await gotoApp(page, 'renderTest=1');
    await waitForBespokeReady(page);

    const size = await page.evaluate(() => {
      const api = (window as Window & {
        __bespoke?: {
          play?: () => void;
          renderOfflineWav?: (opts?: { seconds?: number }) => Uint8Array | null;
        };
      }).__bespoke;
      const wav = api?.renderOfflineWav?.({ seconds: 0.25 });
      return wav?.byteLength ?? 0;
    });

    expect(size).toBeGreaterThan(44);
  });
});
