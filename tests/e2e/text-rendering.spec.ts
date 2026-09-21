/**
 * WebGL text rendering regression test (SDF atlas by default, 5x7 font via ?debugFont=pixel).
 *
 * Run (starts http-server on :9876 automatically):
 *   npm run test:e2e:text
 *
 * Manual server (must serve dist/ on port 9876):
 *   npm run build:web-only && cd dist && python3 -m http.server 9876
 *   PLAYWRIGHT_SKIP_WEBSERVER=1 PLAYWRIGHT_BASE_URL=http://localhost:9876 \\
 *     npx playwright test tests/e2e/text-rendering.spec.ts --project=chromium --grep webgl
 */
import { test, expect } from '@playwright/test';
import * as fs from 'fs';
import * as path from 'path';
import { gotoApp, waitForBespokeReady as waitForReady } from './helpers';

const FIXTURES_DIR = path.join(__dirname, '../fixtures');

function attachInitDiagnostics(page: import('@playwright/test').Page): string[] {
  const errors: string[] = [];
  page.on('console', (msg) => {
    if (msg.type() === 'error') errors.push(`[console] ${msg.text()}`);
  });
  page.on('pageerror', (err) => errors.push(`[pageerror] ${err.message}`));
  return errors;
}

async function waitForBespokeReady(
  page: import('@playwright/test').Page,
  errors: string[],
): Promise<void> {
  try {
    await waitForReady(page);
  } catch (err) {
    const status = await page.locator('#status .status-subheader').textContent().catch(() => '(unknown)');
    const hint = errors.length > 0 ? `\nBrowser errors:\n${errors.slice(-8).join('\n')}` : '';
    throw new Error(
      `BespokeSynth did not finish initializing within 60s (status: ${status}). `
      + 'Ensure dist/ is being served (npm run test:e2e:text).'
      + hint,
      { cause: err },
    );
  }
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

interface TextCase {
  query: string;
  font: 'sdf' | 'pixel';
  fixture: string;
}

async function runTextRenderingCase(page: import('@playwright/test').Page, textCase: TextCase): Promise<void> {
  const errors = attachInitDiagnostics(page);
  await gotoApp(page, textCase.query);
  await waitForBespokeReady(page, errors);

  const reported = await page.evaluate(() => {
    const w = window as Window & { __bespoke?: { getRendererBackend?: () => string; getFontMode?: () => string } };
    return { backend: w.__bespoke?.getRendererBackend?.(), font: w.__bespoke?.getFontMode?.() };
  });
  expect(reported.backend).toBe('webgl');
  expect(reported.font).toBe(textCase.font);

  // The font regression panel always draws with the 5x7 font, in both modes.
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
  const fixturePath = path.join(FIXTURES_DIR, textCase.fixture);
  if (process.env.UPDATE_TEXT_BASELINES === '1' || !fs.existsSync(fixturePath)) {
    fs.writeFileSync(fixturePath, png);
    test.info().annotations.push({
      type: 'note',
      description: `Wrote baseline ${fixturePath}`,
    });
    return;
  }

  // Visual sanity: canvas must contain non-trivial content (garbled/blank frames fail luminance).
  // Use UPDATE_TEXT_BASELINES=1 to refresh tests/fixtures/*.png after intentional UI changes.
  const baseline = fs.readFileSync(fixturePath);
  expect(meanLuminance(png)).toBeGreaterThan(Math.max(8, meanLuminance(baseline) * 0.4));
}

interface TitleExtent {
  type: string;
  top: number;
  bottom: number;
}

/**
 * Vertical extent (CSS px from the title bar top) of each module's title text, found by
 * reading back the canvas and marking pixels that differ strongly from the bar color.
 */
async function measureTitleExtents(page: import('@playwright/test').Page): Promise<TitleExtent[]> {
  await page.evaluate(() => {
    (window as Window & { __bespoke?: { renderFrame?: () => void } }).__bespoke?.renderFrame?.();
  });
  return page.evaluate(() => {
    const TITLE_BAR_HEIGHT = 18; // Module::kTitleBarHeight at 100% zoom
    const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
    const canvas = document.querySelector<HTMLCanvasElement>('#canvas')!;
    const ratio = canvas.width / canvas.getBoundingClientRect().width;
    const copy = document.createElement('canvas');
    copy.width = canvas.width;
    copy.height = canvas.height;
    const ctx = copy.getContext('2d')!;
    ctx.drawImage(canvas, 0, 0);

    const extents: TitleExtent[] = [];
    // Transport and Scale are custom-drawn panels without a title bar.
    const untitled = new Set(['transport', 'scale']);
    for (const m of api.getModules() as Array<{ id: number; type: string }>) {
      if (untitled.has(m.type)) continue;
      const info = api.getModuleInfo(m.id) as { screenX: number; screenY: number; width: number };
      // Left part of the bar: the title, clear of the enabled indicator on the right.
      const x0 = Math.round((info.screenX + 4) * ratio);
      const w = Math.round(Math.min(info.width - 24, 90) * ratio);
      const y0 = Math.round(info.screenY * ratio);
      const h = Math.round(TITLE_BAR_HEIGHT * ratio);
      if (w <= 0 || x0 < 0 || y0 < 0) continue;
      const { data } = ctx.getImageData(x0, y0, w, h);

      // Bar color = most common pixel in the region.
      const counts = new Map<number, number>();
      for (let i = 0; i < data.length; i += 4) {
        const key = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        counts.set(key, (counts.get(key) ?? 0) + 1);
      }
      let bg = 0;
      let best = -1;
      counts.forEach((n, key) => {
        if (n > best) {
          best = n;
          bg = key;
        }
      });
      const bgR = bg >> 16;
      const bgG = (bg >> 8) & 0xff;
      const bgB = bg & 0xff;

      let top = -1;
      let bottom = -1;
      for (let row = 0; row < h; row++) {
        for (let col = 0; col < w; col++) {
          const i = (row * w + col) * 4;
          const diff = Math.abs(data[i] - bgR) + Math.abs(data[i + 1] - bgG) + Math.abs(data[i + 2] - bgB);
          if (diff > 120) {
            if (top < 0) top = row;
            bottom = row;
            break;
          }
        }
      }
      if (top >= 0) extents.push({ type: m.type, top: top / ratio, bottom: bottom / ratio });
    }
    return extents;
  });
}

async function loadTitleExtents(page: import('@playwright/test').Page, query: string): Promise<TitleExtent[]> {
  const errors = attachInitDiagnostics(page);
  await gotoApp(page, query);
  await waitForBespokeReady(page, errors);
  await page.waitForTimeout(300);
  return measureTitleExtents(page);
}

test.describe('text rendering (webgl)', () => {
  test('render test scene initializes with legible canvas output (SDF atlas)', async ({ page }) => {
    await runTextRenderingCase(page, { query: 'renderTest=1', font: 'sdf', fixture: 'render-test-webgl-sdf.png' });
  });

  test('debugFont=pixel keeps the 5x7 bitmap font', async ({ page }) => {
    await runTextRenderingCase(page, {
      query: 'renderTest=1&debugFont=pixel',
      font: 'pixel',
      fixture: 'render-test-webgl.png',
    });
  });

  test('module titles stay inside their title bars with SDF and pixel fonts', async ({ page }) => {
    test.setTimeout(120000);
    const sdf = await loadTitleExtents(page, 'renderTest=1');
    const pixel = await loadTitleExtents(page, 'renderTest=1&debugFont=pixel');

    expect(sdf.length).toBeGreaterThanOrEqual(5);
    expect(sdf.length).toBe(pixel.length);
    for (const extent of [...sdf, ...pixel]) {
      // Glyph tops sit ~2 px under the bar top; descenders stay inside the 18 px bar.
      expect(extent.top, `${extent.type} title top`).toBeGreaterThanOrEqual(1);
      expect(extent.top, `${extent.type} title top`).toBeLessThanOrEqual(6);
      expect(extent.bottom, `${extent.type} title bottom`).toBeLessThanOrEqual(16);
    }
    // Same baseline convention: SDF cap tops land within 2 px of the pixel font's.
    for (let i = 0; i < sdf.length; i++) {
      expect(Math.abs(sdf[i].top - pixel[i].top), `${sdf[i].type} SDF vs pixel title top`).toBeLessThanOrEqual(2);
    }
  });
});
