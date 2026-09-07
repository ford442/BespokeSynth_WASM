import { test, expect } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

test.describe('Graph contract', () => {
  test('two overlapping MIDI notes on an uncabled oscillator', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);

    const result = await page.evaluate(async () => {
      const w = window as Window & {
        __bespoke?: {
          createModule?: (type: string, x: number, y: number) => number;
          connectModules?: (src: number, dest: number) => void;
          play?: () => void;
          midiNoteOn?: (channel: number, pitch: number, velocity: number) => void;
          midiNoteOff?: (channel: number, pitch: number) => void;
          getModuleCount?: () => number;
          getRendererBackend?: () => string;
        };
      };
      const api = w.__bespoke;
      if (!api?.createModule || !api.connectModules || !api.play) {
        return { ok: false, reason: 'api missing' };
      }

      const osc = api.createModule('oscillator', 80, 180);
      const out = api.createModule('output', 320, 180);
      if (osc < 0 || out < 0) return { ok: false, reason: 'create failed', osc, out };
      api.connectModules(osc, out);
      api.play();
      api.midiNoteOn?.(0, 60, 1);
      api.midiNoteOn?.(0, 64, 1);
      await new Promise((resolve) => setTimeout(resolve, 250));
      api.midiNoteOff?.(0, 60);
      await new Promise((resolve) => setTimeout(resolve, 100));
      api.midiNoteOff?.(0, 64);

      return {
        ok: true,
        osc,
        out,
        count: api.getModuleCount?.() ?? 0,
        backend: api.getRendererBackend?.() ?? '',
      };
    });

    expect(result.ok).toBe(true);
    expect(result.count).toBeGreaterThanOrEqual(4);
    expect(result.backend).toBeTruthy();
  });
});
