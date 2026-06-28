import { expect, type Page } from '@playwright/test';

/** Headless CI uses WebGL2; local runs can override via PLAYWRIGHT_RENDERER=webgpu */
export function testRenderer(): 'webgl' | 'webgpu' {
  const fromEnv = process.env.PLAYWRIGHT_RENDERER;
  if (fromEnv === 'webgpu' || fromEnv === 'webgl') return fromEnv;
  return 'webgl';
}

export async function gotoApp(page: Page, query = ''): Promise<void> {
  const renderer = testRenderer();
  const suffix = query ? `&${query}` : '';
  await page.goto(`/?renderer=${renderer}${suffix}`);
  await expect(page).toHaveTitle(/BespokeSynth WASM/i, { timeout: 5000 });
  await expect(page.locator('#canvas')).toBeVisible({ timeout: 5000 });
}

export async function waitForBespokeReady(page: Page, timeout = 60000): Promise<void> {
  await page.waitForFunction(
    () => (window as Window & { __bespoke?: { getRendererBackend?: () => string } }).__bespoke?.getRendererBackend !== undefined,
    { timeout },
  );
}