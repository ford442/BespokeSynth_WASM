import { test, expect, type Page } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

type BespokeApi = {
  getStateJson?: () => string | null;
  setControlValue?: (moduleId: number, name: string, value: number) => void;
  getControlValue?: (moduleId: number, name: string) => number;
  renderOfflineWav?: (opts?: { seconds?: number; bitsPerSample?: 16 }) => Uint8Array | null;
};

/** Per-channel RMS of a 16-bit stereo offline render of the current graph. */
async function renderChannelRms(page: Page): Promise<{ left: number; right: number } | null> {
  return page.evaluate(() => {
    const api = (window as Window & { __bespoke?: BespokeApi }).__bespoke;
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
        const frames = Math.floor(size / 4);
        let sumL = 0;
        let sumR = 0;
        for (let i = 0; i < frames; i++) {
          const l = view.getInt16(offset + 8 + i * 4, true) / 32768;
          const r = view.getInt16(offset + 8 + i * 4 + 2, true) / 32768;
          sumL += l * l;
          sumR += r * r;
        }
        return { left: Math.sqrt(sumL / Math.max(1, frames)), right: Math.sqrt(sumR / Math.max(1, frames)) };
      }
      offset += 8 + size + (size % 2);
    }
    return null;
  });
}

test.describe('Stereo bus (?renderTest=2)', () => {
  test('mixer pan moves the drum bus between channels', async ({ page }) => {
    await gotoApp(page, 'renderTest=2');
    await waitForBespokeReady(page);

    const mixerId = await page.evaluate(() => {
      const api = (window as Window & { __bespoke?: BespokeApi }).__bespoke;
      const state = JSON.parse(api?.getStateJson?.() ?? '{}') as { modules?: { id: number; type: string }[] };
      return state.modules?.find((m) => m.type === 'mixer')?.id ?? -1;
    });
    expect(mixerId).toBeGreaterThanOrEqual(0);

    const centred = await renderChannelRms(page);
    expect(centred).toBeTruthy();
    expect(centred!.left).toBeGreaterThan(0.01);
    expect(Math.abs(centred!.left - centred!.right)).toBeLessThan(centred!.left * 0.01);

    const panned = await page.evaluate((id) => {
      const api = (window as Window & { __bespoke?: BespokeApi }).__bespoke;
      for (const name of ['pan1', 'pan2', 'pan3', 'pan4']) api?.setControlValue?.(id, name, 0);
      return api?.getControlValue?.(id, 'pan1');
    }, mixerId);
    expect(panned).toBe(0);

    const left = await renderChannelRms(page);
    expect(left).toBeTruthy();
    expect(left!.left).toBeGreaterThan(0.01);
    expect(left!.right).toBeLessThan(left!.left * 0.01);
  });
});
