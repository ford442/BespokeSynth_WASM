import { test, expect } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

test.describe('Step sequencer', () => {
  test('renderTest scene includes sequencer and plays without MIDI', async ({ page }) => {
    await gotoApp(page, 'renderTest=1');
    await waitForBespokeReady(page);

    const snapshot = await page.evaluate(() => {
      const w = window as Window & {
        __bespoke?: {
          getModuleCount?: () => number;
          getStateJson?: () => string | null;
        };
      };
      const api = w.__bespoke;
      const count = api?.getModuleCount?.() ?? 0;
      const stateJson = api?.getStateJson?.() ?? '';
      const hasSequencer = stateJson.includes('stepsequencer');
      const patternMatch = stateJson.match(/"pattern"\s*:\s*([0-9.eE+-]+)/);
      const pattern = patternMatch ? Number(patternMatch[1]) : 0;
      return { count, hasSequencer, pattern, stateLen: stateJson.length };
    });

    expect(snapshot.hasSequencer).toBe(true);
    expect(snapshot.pattern).toBeGreaterThan(0);
    // transport + scale + seq + osc + filter + gain + lfo + out
    expect(snapshot.count).toBeGreaterThanOrEqual(6);

    await page.locator('#playBtn').click();
    await page.waitForTimeout(400);

    // Transport play should keep the canvas ready without throwing.
    const stillReady = await page.evaluate(() => {
      const w = window as Window & {
        __bespoke?: { getRendererBackend?: () => string; getModuleCount?: () => number };
      };
      return {
        backend: w.__bespoke?.getRendererBackend?.(),
        count: w.__bespoke?.getModuleCount?.() ?? 0,
      };
    });
    expect(stillReady.backend).toBeTruthy();
    expect(stillReady.count).toBeGreaterThanOrEqual(6);
  });
});
