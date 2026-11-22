import { test, expect } from '@playwright/test';

test.describe('WebGPU → WebGL2 fallback', () => {
  const hideWebGpu = () =>
    // `gpu` is a getter on Navigator.prototype where WebGPU is enabled; deleting it from the
    // instance would be a no-op there.
    Object.defineProperty(Navigator.prototype, 'gpu', { configurable: true, get: () => undefined });

  test('does not silently fall back: shows a WebGPU-unavailable notice with an opt-in button', async ({ page }) => {
    test.setTimeout(120000);
    await page.addInitScript(hideWebGpu);

    await page.goto('/');
    await expect(page).toHaveTitle(/BespokeSynth WASM/i, { timeout: 5000 });

    await expect(page.locator('#status.error')).toBeVisible({ timeout: 90000 });
    await expect(page.locator('#status .status-header')).toHaveText('WebGPU Unavailable');
    await expect(page.locator('.init-retry-btn')).toHaveText('Enable WebGL2 fallback');
    // No renderer is running, so nothing may claim WebGPU (or WebGL2) acceleration.
    await expect(page.locator('#footer-info')).not.toContainText('WebGPU-accelerated');
    await expect(page.locator('#footer-info')).toContainText('WebGPU unavailable');
    await expect(page.locator('.renderer-fallback-badge')).toHaveCount(0);
    expect(
      await page.evaluate(() => (window as Window & { usingWebGL?: boolean }).usingWebGL ?? false),
    ).toBe(false);
  });

  test('the notice button opts in to WebGL2 and the UI says so', async ({ page }) => {
    test.setTimeout(120000);
    await page.addInitScript(hideWebGpu);

    await page.goto('/');
    await page.locator('.init-retry-btn').click({ timeout: 90000 });

    await page.waitForFunction(
      () => window.__bespoke?.getRendererBackend() === 'webgl' &&
        document.querySelector('#status')?.classList.contains('hidden') === true,
      { timeout: 90000 },
    );
    await expect(page.locator('.renderer-fallback-badge')).toHaveText('WebGL2');
    await expect(page.locator('#footer-info')).toContainText('WebGL2');
    await expect(page.locator('#footer-info')).not.toContainText('WebGPU-accelerated');
  });

  test('?webgl2=1 opts in to WebGL2 without a prompt', async ({ page }) => {
    test.setTimeout(120000);
    await page.addInitScript(hideWebGpu);

    await page.goto('/?webgl2=1');
    await page.waitForFunction(
      () => window.__bespoke?.getRendererBackend() === 'webgl' &&
        document.querySelector('#status')?.classList.contains('hidden') === true,
      { timeout: 90000 },
    );
    await expect(page.locator('.renderer-fallback-badge')).toHaveText('WebGL2');
    await expect(page.locator('#footer-info')).toContainText('Renderer: WebGL2');
  });

  test('shows recovery button when WebGPU fails and fallback is blocked', async ({ page }) => {
    test.setTimeout(120000);

    await page.addInitScript(() => {
      // `gpu` is a getter on Navigator.prototype where WebGPU is enabled; deleting it from the
      // instance would be a no-op there.
      Object.defineProperty(Navigator.prototype, 'gpu', { configurable: true, get: () => undefined });
    });

    await page.goto('/?renderer=webgpu');
    await expect(page).toHaveTitle(/BespokeSynth WASM/i, { timeout: 5000 });

    await expect(page.locator('#status.error')).toBeVisible({ timeout: 90000 });
    await expect(page.locator('.init-recovery-hint')).toBeVisible();
    await expect(page.locator('.init-retry-btn')).toBeVisible();
  });
});
