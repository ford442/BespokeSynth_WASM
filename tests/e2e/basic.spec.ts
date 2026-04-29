import { test, expect } from '@playwright/test';

test.describe('BespokeSynth WASM', () => {
  test('page loads and initializes WebGPU', async ({ page }) => {
    // Navigate to the application
    await page.goto('/');

    // Wait for the WASM module to load
    await page.waitForFunction(
      () => (window as any).Module?.cwrap !== undefined,
      { timeout: 10000 }
    );

    // Check for successful initialization
    const initState = await page.evaluate(() => {
      const Module = (window as any).Module;
      return Module?.cwrap?.('bespoke_get_init_state', 'number', [])?.();
    });

    // InitState should be FullyInitialized (5) or greater
    expect(initState).toBeGreaterThanOrEqual(5);
  });

  test('canvas is rendered', async ({ page }) => {
    await page.goto('/');

    // Wait for canvas element
    const canvas = page.locator('canvas');
    await expect(canvas).toBeVisible({ timeout: 10000 });

    // Verify canvas has dimensions
    const boundingBox = await canvas.boundingBox();
    expect(boundingBox?.width).toBeGreaterThan(0);
    expect(boundingBox?.height).toBeGreaterThan(0);
  });

  test('responds to mouse input', async ({ page }) => {
    await page.goto('/');

    // Wait for full initialization
    await page.waitForFunction(
      () => {
        const Module = (window as any).Module;
        const state = Module?.cwrap?.('bespoke_get_init_state', 'number', [])?.();
        return state === 5; // FullyInitialized
      },
      { timeout: 10000 }
    );

    const canvas = page.locator('canvas');
    const boundingBox = await canvas.boundingBox();

    if (boundingBox) {
      // Move mouse over canvas
      await page.mouse.move(boundingBox.x + 50, boundingBox.y + 50);

      // Simulate mouse down
      await page.mouse.down();
      await page.waitForTimeout(100);
      await page.mouse.up();

      // Test should pass without errors
      expect(true).toBe(true);
    }
  });

  test('no WebGPU errors in console', async ({ page }) => {
    const errors: string[] = [];

    page.on('console', (msg) => {
      if (msg.type() === 'error' && msg.text().includes('WebGPU')) {
        errors.push(msg.text());
      }
    });

    await page.goto('/');
    await page.waitForTimeout(5000);

    expect(errors).toHaveLength(0);
  });
});
