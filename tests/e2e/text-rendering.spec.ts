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

async function runTextRenderingCase(page: import('@playwright/test').Page): Promise<void> {
  const errors = attachInitDiagnostics(page);
  await gotoApp(page, 'renderTest=1');
  await waitForBespokeReady(page, errors);

  const reportedBackend = await page.evaluate(() => {
    const w = window as Window & { __bespoke?: { getRendererBackend?: () => string } };
    return w.__bespoke?.getRendererBackend?.();
  });
  expect(reportedBackend).toBe('webgl');

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
  const fixturePath = path.join(FIXTURES_DIR, 'render-test-webgl.png');
  if (process.env.UPDATE_TEXT_BASELINES === '1' || !fs.existsSync(fixturePath)) {
    fs.writeFileSync(fixturePath, png);
    test.info().annotations.push({
      type: 'note',
      description: `Wrote baseline ${fixturePath}`,
    });
    return;
  }

  // Visual sanity: canvas must contain non-trivial content (garbled/blank frames fail luminance).
  // Use UPDATE_TEXT_BASELINES=1 to refresh tests/fixtures/render-test-webgl.png after intentional UI changes.
  const baseline = fs.readFileSync(fixturePath);
  expect(meanLuminance(png)).toBeGreaterThan(Math.max(8, meanLuminance(baseline) * 0.4));
}

test.describe('text rendering (webgl)', () => {
  test('render test scene initializes with legible canvas output', async ({ page }) => {
    await runTextRenderingCase(page);
  });
});