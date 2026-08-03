import { test, expect } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

test.describe('Step sequencer', () => {
  test('renderTest scene includes sequencer and plays without MIDI', async ({ page }) => {
    await gotoApp(page, 'renderTest=1');
    await waitForBespokeReady(page);

    const result = await page.evaluate(async () => {
      const w = window as Window & {
        __bespoke?: {
          getModuleCount?: () => number;
          getStateJson?: () => string | null;
          getModule?: () => {
            _bespoke_play: () => void;
            _bespoke_get_cpu_load: () => number;
          } | null;
        };
      };

      const api = w.__bespoke;
      if (!api?.getModule) return { ok: false, reason: 'no api' };

      const mod = api.getModule();
      if (!mod) return { ok: false, reason: 'no module' };

      const countBefore = api.getModuleCount?.() ?? 0;
      const stateJson = api.getStateJson?.() ?? '';
      const hasSequencer = stateJson.includes('stepsequencer');
      const patternMatch = stateJson.match(/"pattern"\s*:\s*([0-9.]+)/);
      const pattern = patternMatch ? Number(patternMatch[1]) : 0;

      mod._bespoke_play();
      await new Promise((r) => setTimeout(r, 500));
      const load = mod._bespoke_get_cpu_load();

      return {
        ok: true,
        countBefore,
        hasSequencer,
        pattern,
        load,
      };
    });

    expect(result.ok).toBe(true);
    expect(result.hasSequencer).toBe(true);
    expect(result.pattern).toBeGreaterThan(0);
    // transport + scale + seq + osc + filter + gain + lfo + out
    expect(result.countBefore).toBeGreaterThanOrEqual(6);
    expect(result.load).toBeGreaterThanOrEqual(0);
  });
});
