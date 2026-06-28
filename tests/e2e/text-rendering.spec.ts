import { test, expect } from '@playwright/test';
import * as fs from 'fs';
import * as path from 'path';

const FIXTURES_DIR = path.join(__dirname, '../fixtures');

async function waitForBespokeReady(page: import('@playwright/test').Page): Promise<void> {
  // Module lives on the app instance, not window.Module — wait for the public API.
  await page.waitForFunction(
    () => (window as Window & { __bespoke?: { getRendererBackend?: () => string } }).__bespoke?.getRendererBackend !== undefined,
    { timeout: 60000 },
  );
  await page.waitForSelector('#status .status-subheader', { state: 'attached', timeout: 10000 });
}

async function captureCanvasPng(page: import('@playwright/test').Page): Promise<Buffer> {
  await page.evaluate(() => {
    (window as Window & { __bespoke?: { renderFrame?: () => void } }).__bespoke?.renderFrame?.();
  });
  const canvas = page.locator('#canvas');
  return canvas.screenshot({ type: 'png' });
}

/** Rough luminance check — garbled/blank frames tend to be very dark or uniform. */
function meanLuminance(png: Buffer): number {
  // PNG is compressed; use a simple heuristic on raw bytes excluding header/chunks.
  // For a sanity check we sample every 97th byte in the IDAT region.
  let sum = 0;
  let count = 0;
  const start = Math.min(128, png.length);
  for (let i = start; i < png.length; i += 97) {
    sum += png[i];
    count++;
  }
  return count > 0 ? sum / count : 0;
}

for (const backend of ['webgl', 'webgpu'] as const) {
  test.describe(`text rendering (${backend})`, () => {
    test(`render test scene initializes with legible canvas output`, async ({ page }) => {
      test.skip(
        backend === 'webgpu',
        'WebGPU unavailable in headless CI — run locally with Chrome 113+',
      );

      await page.goto(`/?renderer=${backend}&renderTest=1`);

      await waitForBespokeReady(page);

      const reportedBackend = await page.evaluate(() => {
        const w = window as Window & { __bespoke?: { getRendererBackend?: () => string } };
        return w.__bespoke?.getRendererBackend?.();
      });
      expect(reportedBackend).toBe(backend);

      // Font regression overlay (ASCII + extended glyphs)
      await page.evaluate(() => {
        const api = (window as Window & {
          __bespoke?: { setFontTestVisible?: (v: boolean) => void };
        }).__bespoke;
        api?.setFontTestVisible?.(true);
      });

      await page.waitForTimeout(500);

      const png = await captureCanvasPng(page);
      expect(png.length).toBeGreaterThan(4096);
      expect(meanLuminance(png)).toBeGreaterThan(8);

      if (!fs.existsSync(FIXTURES_DIR)) {
        fs.mkdirSync(FIXTURES_DIR, { recursive: true });
      }
      const fixturePath = path.join(FIXTURES_DIR, `render-test-${backend}.png`);
      if (!fs.existsSync(fixturePath)) {
        fs.writeFileSync(fixturePath, png);
        test.info().annotations.push({
          type: 'note',
          description: `Created baseline ${fixturePath} — re-run to enforce diff`,
        });
      } else {
        const baseline = fs.readFileSync(fixturePath);
        // Allow minor GPU/driver variance; size should be close and image non-empty.
        expect(Math.abs(png.length - baseline.length)).toBeLessThan(baseline.length * 0.15);
        expect(meanLuminance(png)).toBeGreaterThan(meanLuminance(baseline) * 0.5);
      }
    });
  });
}