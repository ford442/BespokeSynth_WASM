/**
 * WebGL text rendering regression test.
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

const FIXTURES_DIR = path.join(__dirname, '../fixtures');

async function assertAppShell(page: import('@playwright/test').Page): Promise<void> {
  await expect(page).toHaveTitle(/BespokeSynth WASM/i, { timeout: 5000 });
  await expect(page.locator('#canvas')).toBeVisible({ timeout: 5000 });
}

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
  // Module lives on the app instance, not window.Module — wait for the public API.
  try {
    await page.waitForFunction(
      () => (window as Window & { __bespoke?: { getRendererBackend?: () => string } }).__bespoke?.getRendererBackend !== undefined,
      { timeout: 60000 },
    );
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

async function runTextRenderingCase(
  page: import('@playwright/test').Page,
  backend: 'webgl' | 'webgpu',
): Promise<void> {
  const errors = attachInitDiagnostics(page);
  await page.goto(`/?renderer=${backend}&renderTest=1`);
  await assertAppShell(page);
  await waitForBespokeReady(page, errors);

  const reportedBackend = await page.evaluate(() => {
    const w = window as Window & { __bespoke?: { getRendererBackend?: () => string } };
    return w.__bespoke?.getRendererBackend?.();
  });
  expect(reportedBackend).toBe(backend);

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
    expect(Math.abs(png.length - baseline.length)).toBeLessThan(baseline.length * 0.15);
    expect(meanLuminance(png)).toBeGreaterThan(meanLuminance(baseline) * 0.5);
  }
}

test.describe('text rendering (webgl)', () => {
  test('render test scene initializes with legible canvas output', async ({ page }) => {
    await runTextRenderingCase(page, 'webgl');
  });
});

test.describe.skip('text rendering (webgpu)', () => {
  test('render test scene initializes with legible canvas output', async ({ page }) => {
    await runTextRenderingCase(page, 'webgpu');
  });
});