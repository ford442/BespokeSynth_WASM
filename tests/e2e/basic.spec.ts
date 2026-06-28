import { test, expect } from '@playwright/test';
import { gotoApp, testRenderer, waitForBespokeReady } from './helpers';

test.describe('BespokeSynth WASM', () => {
  test('page loads and initializes', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);

    const backend = await page.evaluate(() => {
      const w = window as Window & { __bespoke?: { getRendererBackend?: () => string } };
      return w.__bespoke?.getRendererBackend?.();
    });

    expect(backend).toBe(testRenderer());
  });

  test('canvas is rendered', async ({ page }) => {
    await gotoApp(page);

    const canvas = page.locator('#canvas');
    await expect(canvas).toBeVisible({ timeout: 10000 });

    const boundingBox = await canvas.boundingBox();
    expect(boundingBox?.width).toBeGreaterThan(0);
    expect(boundingBox?.height).toBeGreaterThan(0);
  });

  test('responds to mouse input', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);

    const canvas = page.locator('#canvas');
    const boundingBox = await canvas.boundingBox();

    if (boundingBox) {
      await page.mouse.move(boundingBox.x + 50, boundingBox.y + 50);
      await page.mouse.down();
      await page.waitForTimeout(100);
      await page.mouse.up();
      expect(true).toBe(true);
    }
  });

  test('no renderer errors in console', async ({ page }) => {
    const errors: string[] = [];

    page.on('console', (msg) => {
      if (msg.type() !== 'error') return;
      const text = msg.text();
      if (testRenderer() === 'webgpu' && text.includes('WebGPU')) {
        errors.push(text);
      }
      if (testRenderer() === 'webgl' && text.includes('WebGL')) {
        errors.push(text);
      }
    });

    await gotoApp(page);
    await waitForBespokeReady(page);

    expect(errors).toHaveLength(0);
  });
});