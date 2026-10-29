import { test, expect } from '@playwright/test';
import { AUDIO_BACKENDS, gotoApp, waitForBespokeReady } from './helpers';

// Default view (no pan, scale 1): screen = world, below the 40 px canvas header.
const CANVAS_HEADER = 40;
const GAIN_X = 600;
const GAIN_Y = 420;
// GainModule's slider: 10 px in, 20 px under its 18 px title bar, 100 x 16.
const SLIDER_LEFT = GAIN_X + 10;
const SLIDER_MID_Y = GAIN_Y + 18 + 20 + 8;
const SLIDER_WIDTH = 100;

test.describe('Engine foundation (#125)', () => {
  for (const audio of AUDIO_BACKENDS) {
    test(`a 10 s knob drag at 60 Hz recompiles nothing [${audio}]`, async ({ page }) => {
      test.setTimeout(90_000);
      await gotoApp(page, `audio=${audio}`);
      await waitForBespokeReady(page);

      const gainId = await page.evaluate(
        ([x, y]) => window.__bespoke?.createModule('gain', x, y) ?? -1,
        [GAIN_X, GAIN_Y],
      );
      expect(gainId).toBeGreaterThanOrEqual(0);
      await page.locator('#playBtn').click();
      await page.waitForTimeout(300);

      const before = await page.evaluate(() => window.__bespoke?.getEngineStats());
      expect(before).toBeTruthy();

      // Pointer Events from a 60 Hz timer in the page for 10 s of wall time (a CDP mouse move per
      // frame is far slower; a loaded headless page may still deliver fewer ticks than 600).
      const moves = await page.evaluate(
        async ({ x0, y, width, header }) => {
          const canvas = document.getElementById('canvas');
          if (!canvas) throw new Error('canvas missing');
          const rect = canvas.getBoundingClientRect();
          const send = (type: string, x: number) =>
            canvas.dispatchEvent(
              new PointerEvent(type, {
                bubbles: true,
                pointerId: 1,
                pointerType: 'mouse',
                isPrimary: true,
                clientX: rect.left + x,
                clientY: rect.top + header + y,
                button: 0,
                buttons: type === 'pointerup' ? 0 : 1,
              }),
            );
          send('pointerdown', x0);
          const durationMs = 10_000;
          const start = performance.now();
          const count = await new Promise<number>((resolve) => {
            let sent = 0;
            const timer = window.setInterval(() => {
              const elapsed = performance.now() - start;
              const t = (1 - Math.cos((elapsed / durationMs) * Math.PI * 2 * 5)) / 2; // five sweeps
              send('pointermove', x0 + t * width);
              sent++;
              if (elapsed >= durationMs) {
                window.clearInterval(timer);
                resolve(sent);
              }
            }, 1000 / 60);
          });
          send('pointermove', x0 + width * 0.25);
          send('pointerup', x0 + width * 0.25);
          return count;
        },
        { x0: SLIDER_LEFT + 2, y: SLIDER_MID_Y, width: SLIDER_WIDTH - 4, header: CANVAS_HEADER },
      );
      expect(moves).toBeGreaterThan(60);

      const after = await page.evaluate(() => window.__bespoke?.getEngineStats());
      expect(after).toBeTruthy();
      // The drag reached the gain slider...
      const gain = await page.evaluate((id) => window.__bespoke?.getControlValue(id, 'gain'), gainId);
      expect(gain).toBeCloseTo(0.25, 1);
      expect(after!.paramChangesSent - before!.paramChangesSent).toBeGreaterThan(moves / 2);
      // ...and went through the param fast path: no snapshot, no plan, no state preparation.
      expect(after!.planCompiles).toBe(before!.planCompiles);
      expect(after!.publishCount).toBe(before!.publishCount);
      // With the worklet engine the UI module's engine is idle; the worklet only prepares on a snapshot post.
      if (!after!.remote) expect(after!.prepareCount).toBe(before!.prepareCount);
    });
  }
});
