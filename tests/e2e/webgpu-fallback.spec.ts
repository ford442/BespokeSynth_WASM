import { test, expect } from '@playwright/test';

test.describe('WebGPU → WebGL2 fallback', () => {
  test('auto-falls back when navigator.gpu is unavailable', async ({ page }) => {
    test.setTimeout(120000);

    await page.addInitScript(() => {
      delete (navigator as Navigator & { gpu?: unknown }).gpu;
    });

    await page.goto('/');
    await expect(page).toHaveTitle(/BespokeSynth WASM/i, { timeout: 5000 });
    await expect(page.locator('#canvas')).toBeVisible({ timeout: 5000 });

    await page.waitForFunction(
      () => {
        const w = window as Window & {
          __bespoke?: { getRendererBackend?: () => string };
          rendererFallbackReason?: string | null;
        };
        const backend = w.__bespoke?.getRendererBackend?.();
        const statusHidden = document.querySelector('#status')?.classList.contains('hidden');
        return backend === 'webgl' && statusHidden === true;
      },
      { timeout: 90000 },
    );

    const backend = await page.evaluate(() => {
      const w = window as Window & { __bespoke?: { getRendererBackend?: () => string } };
      return w.__bespoke?.getRendererBackend?.();
    });
    expect(backend).toBe('webgl');

    const fallbackReason = await page.evaluate(() => {
      return (window as Window & { rendererFallbackReason?: string | null }).rendererFallbackReason;
    });
    expect(fallbackReason).toMatch(/WebGPU/i);

    await expect(page.locator('.renderer-fallback-badge')).toBeVisible();
    await expect(page.locator('.renderer-fallback-badge')).toHaveText('WebGL2 fallback');
  });

  test('shows recovery button when WebGPU fails and fallback is blocked', async ({ page }) => {
    test.setTimeout(120000);

    await page.addInitScript(() => {
      delete (navigator as Navigator & { gpu?: unknown }).gpu;
    });

    await page.goto('/?renderer=webgpu');
    await expect(page).toHaveTitle(/BespokeSynth WASM/i, { timeout: 5000 });

    await expect(page.locator('#status.error')).toBeVisible({ timeout: 90000 });
    await expect(page.locator('.init-recovery-hint')).toBeVisible();
    await expect(page.locator('.init-retry-btn')).toBeVisible();
  });
});
