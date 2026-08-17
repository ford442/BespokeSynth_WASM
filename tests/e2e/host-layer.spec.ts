import { test, expect } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

test.describe('Host layer single path', () => {
  test('one mousedown produces exactly one WASM mouse_down call', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);

    const counts = await page.evaluate(async () => {
      const w = window as Window & {
        __bespoke?: {
          resetHostCounters?: () => void;
          getHostMouseDownCount?: () => number;
        };
      };
      w.__bespoke?.resetHostCounters?.();

      const canvas = document.getElementById('canvas');
      if (!canvas) throw new Error('canvas missing');

      const rect = canvas.getBoundingClientRect();
      canvas.dispatchEvent(
        new MouseEvent('mousedown', {
          bubbles: true,
          clientX: rect.left + 40,
          clientY: rect.top + 40,
          button: 0,
        }),
      );

      await new Promise((resolve) => requestAnimationFrame(resolve));

      return {
        mouseDown: w.__bespoke?.getHostMouseDownCount?.() ?? -1,
      };
    });

    expect(counts.mouseDown).toBe(1);
  });

  test('render call count tracks host requestAnimationFrame frames', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);

    const counts = await page.evaluate(async () => {
      const w = window as Window & {
        __bespoke?: {
          getHostRenderCount?: () => number;
          getHostFrameCount?: () => number;
        };
      };

      const waitFrames = (n: number) =>
        new Promise<void>((resolve) => {
          let remaining = n;
          const tick = () => {
            remaining -= 1;
            if (remaining <= 0) resolve();
            else requestAnimationFrame(tick);
          };
          requestAnimationFrame(tick);
        });

      const startWasm = w.__bespoke?.getHostRenderCount?.() ?? 0;
      const startHost = w.__bespoke?.getHostFrameCount?.() ?? 0;

      await waitFrames(8);

      return {
        wasmDelta: (w.__bespoke?.getHostRenderCount?.() ?? 0) - startWasm,
        hostDelta: (w.__bespoke?.getHostFrameCount?.() ?? 0) - startHost,
      };
    });

    expect(counts.wasmDelta).toBeGreaterThanOrEqual(5);
    expect(counts.hostDelta).toBeGreaterThanOrEqual(5);
    expect(Math.abs(counts.wasmDelta - counts.hostDelta)).toBeLessThanOrEqual(2);
  });
});
