import { test, expect, type Page } from '@playwright/test';
import { gotoApp, testRenderer, waitForBespokeReady } from './helpers';

/** Decodes a PNG data URL in the page and reports its size and alpha range. */
async function pngStats(page: Page, dataUrl: string) {
  return page.evaluate(async (url) => {
    const img = new Image();
    img.src = url;
    await img.decode();
    const canvas = document.createElement('canvas');
    canvas.width = img.width;
    canvas.height = img.height;
    const ctx = canvas.getContext('2d');
    if (!ctx) throw new Error('2D context unavailable');
    ctx.drawImage(img, 0, 0);
    const { data } = ctx.getImageData(0, 0, img.width, img.height);
    let minAlpha = 255;
    let lit = 0;
    for (let i = 0; i < data.length; i += 4) {
      minAlpha = Math.min(minAlpha, data[i + 3]);
      if (data[i] + data[i + 1] + data[i + 2] > 60) lit += 1;
    }
    return { width: img.width, height: img.height, minAlpha, lit };
  }, dataUrl);
}

async function webgpuAvailable(page: Page): Promise<boolean> {
  return page.evaluate(async () => {
    const gpu = (navigator as Navigator & { gpu?: { requestAdapter(): Promise<unknown> } }).gpu;
    return !!gpu && !!(await gpu.requestAdapter().catch(() => null));
  });
}

/**
 * Whether WebGPU can present frames to a canvas here. Headless Chromium with SwiftShader
 * cannot allocate the swapchain image and loses the device (reason "destroyed") within a
 * few frames, which the app then recovers from on its own; a destroy() test is moot there.
 */
async function webgpuCanPresent(page: Page): Promise<boolean> {
  return page.evaluate(async () => {
    type Gpu = {
      requestAdapter(): Promise<{ requestDevice(): Promise<GpuDevice> } | null>;
      getPreferredCanvasFormat(): string;
    };
    type GpuDevice = {
      lost: Promise<unknown>;
      destroy(): void;
      createCommandEncoder(): {
        beginRenderPass(d: unknown): { end(): void };
        finish(): unknown;
      };
      queue: { submit(b: unknown[]): void };
    };
    const gpu = (navigator as Navigator & { gpu?: Gpu }).gpu;
    const adapter = await gpu?.requestAdapter().catch(() => null);
    if (!gpu || !adapter) return false;
    const device = await adapter.requestDevice();
    const state = { lost: false };
    void device.lost.then(() => (state.lost = true));
    const canvas = document.createElement('canvas');
    const context = canvas.getContext('webgpu') as unknown as {
      configure(c: unknown): void;
      getCurrentTexture(): { createView(): unknown };
    } | null;
    if (!context) return false;
    context.configure({ device, format: gpu.getPreferredCanvasFormat(), alphaMode: 'opaque' });
    for (let i = 0; i < 5 && !state.lost; i += 1) {
      const encoder = device.createCommandEncoder();
      const view = context.getCurrentTexture().createView();
      encoder.beginRenderPass({ colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store' }] }).end();
      device.queue.submit([encoder.finish()]);
      await new Promise((resolve) => requestAnimationFrame(resolve));
    }
    await new Promise((resolve) => setTimeout(resolve, 200));
    const ok = !state.lost;
    device.destroy();
    return ok;
  });
}

function collectPageErrors(page: Page): string[] {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error' && /Uncaught|Unhandled/i.test(message.text())) errors.push(message.text());
  });
  return errors;
}

test.describe('Runtime safety nets', () => {
  test('captureScreenshot returns an opaque PNG of the frame', async ({ page }) => {
    test.setTimeout(120000);
    await gotoApp(page);
    await waitForBespokeReady(page);

    const dataUrl = await page.evaluate(() => window.__bespoke?.captureScreenshot({ x: 0, y: 0, width: 200, height: 120 }));
    expect(dataUrl).toMatch(/^data:image\/png;base64,/);
    const stats = await pngStats(page, dataUrl ?? '');
    expect(stats.width).toBe(200);
    expect(stats.height).toBe(120);
    // The canvas is opaque on both backends: nothing of the page shows through a screenshot.
    expect(stats.minAlpha).toBe(255);
    expect(stats.lit).toBeGreaterThan(0);
  });

  test('a mixer takes 16 audio cables (no fan-in limit)', async ({ page }) => {
    test.setTimeout(120000);
    await page.goto('/?renderer=webgl');
    await waitForBespokeReady(page);

    const result = await page.evaluate(() => {
      const api = window.__bespoke;
      if (!api) throw new Error('__bespoke missing');
      const mixer = api.createModule('mixer', 500, 100);
      for (let i = 0; i < 16; i += 1) {
        const osc = api.createModule('oscillator', 40, 30 * i);
        api.connectModules(osc, mixer);
      }
      const graph = JSON.parse(api.getStateJson()) as { connections?: Array<{ dest_module_id?: number }> };
      const inputs = (graph.connections ?? []).filter((c) => c.dest_module_id === mixer).length;
      return { inputs };
    });

    // Every cable is kept; the plan's input lists are sized when it compiles.
    expect(result.inputs).toBe(16);
  });

  test('a failing worklet addModule falls back to SDL audio without an unhandled rejection', async ({ page }) => {
    test.setTimeout(120000);
    const errors = collectPageErrors(page);
    await page.addInitScript(() => {
      const original = AudioWorklet.prototype.addModule;
      AudioWorklet.prototype.addModule = function (url: string | URL, options?: WorkletOptions) {
        if (String(url).includes('ringPlayer')) return Promise.reject(new Error('mocked addModule failure'));
        return original.call(this, url, options);
      };
    });
    // Renderer-independent; WebGL2 keeps a headless WebGPU device loss from replacing the toast.
    await page.goto('/?renderer=webgl&audio=ring');
    await waitForBespokeReady(page);

    // Two clicks in a row: the second joins the first start instead of opening another context.
    await page.evaluate(() => {
      void window.__bespoke?.play();
      return window.__bespoke?.play();
    });
    await expect(page.locator('#patch-status-toast')).toContainText('using SDL2');

    await expect
      .poll(() => page.evaluate(() => window.__bespoke?.getAudioHealth()?.backend), { timeout: 10000 })
      .toBe(0);
    const before = await page.evaluate(() => window.__bespoke?.getAudioHealth()?.callbackCount ?? 0);
    await expect
      .poll(() => page.evaluate(() => window.__bespoke?.getAudioHealth()?.callbackCount ?? 0), { timeout: 10000 })
      .toBeGreaterThan(before);
    expect(errors).toEqual([]);
  });

  test('a lost WebGL2 context is restored on a fresh canvas', async ({ page }) => {
    test.setTimeout(120000);
    test.skip(testRenderer() !== 'webgl', 'WebGL2 context loss');
    await gotoApp(page, 'patch=starter');
    await waitForBespokeReady(page);
    const modulesBefore = await page.evaluate(() => window.__bespoke?.getModules().length ?? 0);

    await page.evaluate(() => {
      const canvas = document.getElementById('canvas') as HTMLCanvasElement;
      canvas.getContext('webgl2')?.getExtension('WEBGL_lose_context')?.loseContext();
    });

    await expect
      .poll(() => page.evaluate(() => (window as Window & { rendererFallbackReason?: string }).rendererFallbackReason), {
        timeout: 15000,
      })
      .toMatch(/WebGL2 context lost/);
    const frames = await page.evaluate(() => window.__bespoke?.getHostFrameCount() ?? 0);
    await expect.poll(() => page.evaluate(() => window.__bespoke?.getHostFrameCount() ?? 0)).toBeGreaterThan(frames + 5);
    expect(await page.evaluate(() => window.__bespoke?.getModules().length ?? 0)).toBe(modulesBefore);
    const dataUrl = await page.evaluate(() => window.__bespoke?.captureScreenshot({ x: 0, y: 0, width: 64, height: 64 }));
    expect((await pngStats(page, dataUrl ?? '')).minAlpha).toBe(255);
  });
});

test.describe('WebGPU failure paths', () => {
  test('GPUDevice.destroy() switches the renderer to WebGL2 and keeps the patch', async ({ page }) => {
    test.setTimeout(120000);
    await page.addInitScript(() => {
      const gpuAdapter = (globalThis as { GPUAdapter?: { prototype: { requestDevice: (...a: unknown[]) => Promise<unknown> } } })
        .GPUAdapter;
      if (!gpuAdapter) return;
      const original = gpuAdapter.prototype.requestDevice;
      gpuAdapter.prototype.requestDevice = async function (...args: unknown[]) {
        const device = await original.apply(this, args);
        (window as Window & { __testGpuDevice?: unknown }).__testGpuDevice = device;
        return device;
      };
    });
    await page.goto('/?renderer=webgpu&patch=starter');
    test.skip(!(await webgpuCanPresent(page)), 'WebGPU cannot present to a canvas in this browser');
    await waitForBespokeReady(page, 90000);
    expect(await page.evaluate(() => window.__bespoke?.getRendererBackend())).toBe('webgpu');
    const modulesBefore = await page.evaluate(() => window.__bespoke?.getModules().length ?? 0);

    await page.evaluate(() => {
      (window as Window & { __testGpuDevice?: { destroy(): void } }).__testGpuDevice?.destroy();
    });

    await expect
      .poll(() => page.evaluate(() => window.__bespoke?.getRendererBackend()), { timeout: 15000 })
      .toBe('webgl');
    const frames = await page.evaluate(() => window.__bespoke?.getHostFrameCount() ?? 0);
    await expect.poll(() => page.evaluate(() => window.__bespoke?.getHostFrameCount() ?? 0)).toBeGreaterThan(frames + 5);
    expect(await page.evaluate(() => window.__bespoke?.getModules().length ?? 0)).toBe(modulesBefore);
    await expect(page.locator('#status')).toHaveClass(/hidden/);
    await expect(page.locator('.renderer-fallback-badge')).toHaveText('WebGL2 fallback');
  });

  test('a WGSL compile error fails init and falls back to WebGL2', async ({ page }) => {
    test.setTimeout(120000);
    await page.addInitScript(() => {
      const gpuDevice = (globalThis as { GPUDevice?: { prototype: { createShaderModule: (d: { code: string }) => unknown } } })
        .GPUDevice;
      if (!gpuDevice) return;
      const original = gpuDevice.prototype.createShaderModule;
      gpuDevice.prototype.createShaderModule = function (descriptor: { code: string }) {
        return original.call(this, { ...descriptor, code: `${descriptor.code}\nfn broken( {` });
      };
    });
    await page.goto('/');
    test.skip(!(await webgpuAvailable(page)), 'no WebGPU adapter in this browser');
    await waitForBespokeReady(page, 90000);

    expect(await page.evaluate(() => window.__bespoke?.getRendererBackend())).toBe('webgl');
    expect(await page.evaluate(() => (window as Window & { rendererFallbackReason?: string }).rendererFallbackReason)).toMatch(
      /WebGPU/,
    );
  });
});

test('the static server is cross-origin isolated', async ({ page }) => {
  await page.goto('/');
  expect(await page.evaluate(() => window.crossOriginIsolated)).toBe(true);
});
