import { test, expect } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

test.describe('Audio worklet path', () => {
  test('exposes audio health metrics on SDL path', async ({ page }) => {
    await gotoApp(page, 'audio=sdl&renderTest=1');
    await waitForBespokeReady(page);

    await page.locator('#playBtn').click();
    await page.waitForTimeout(600);

    const health = await page.evaluate(() => {
      const w = window as Window & {
        __bespoke?: { getAudioHealth?: () => Record<string, number> | null; play?: () => Promise<void> };
      };
      return w.__bespoke?.getAudioHealth?.() ?? null;
    });

    expect(health).toBeTruthy();
    expect(health!.callbackCount).toBeGreaterThan(0);
    expect(Number.isFinite(health!.cpuLoad)).toBe(true);
    expect(health!.cpuLoad).toBeGreaterThanOrEqual(0);
    expect(health!.maxProcessTimeMs).toBeGreaterThanOrEqual(0);
  });

  test('AudioWorklet POC keeps producing frames during main-thread stall', async ({ page }) => {
    await gotoApp(page, 'audioWorkletPoc=1');
    await waitForBespokeReady(page);

    const result = await page.evaluate(async () => {
      const w = window as Window & {
        __bespoke?: {
          runAudioWorkletPoc?: (opts?: { stallMs?: number; settleMs?: number }) => Promise<{
            ok: boolean;
            reason?: string;
            framesDelta: number;
            expectedMinDelta: number;
          }>;
        };
        __bespokeAudioWorkletPoc?: { ok: boolean; framesDelta: number; expectedMinDelta: number };
      };
      if (w.__bespokeAudioWorkletPoc) return w.__bespokeAudioWorkletPoc;
      return w.__bespoke?.runAudioWorkletPoc?.({ stallMs: 100, settleMs: 250 }) ?? {
        ok: false,
        reason: 'no api',
        framesDelta: 0,
        expectedMinDelta: 0,
      };
    });

    expect(result.ok, result.reason || 'POC failed').toBe(true);
    expect(result.framesDelta).toBeGreaterThanOrEqual(result.expectedMinDelta);
  });

  test('worklet SAB ring path initializes and plays canonical patch', async ({ page }) => {
    await gotoApp(page, 'audio=worklet&renderTest=1');
    await waitForBespokeReady(page);

    const isolated = await page.evaluate(() => ({
      coi: (window as unknown as { crossOriginIsolated?: boolean }).crossOriginIsolated === true,
      sab: typeof SharedArrayBuffer !== 'undefined',
    }));
    expect(isolated.coi).toBe(true);
    expect(isolated.sab).toBe(true);

    await page.evaluate(async () => {
      const w = window as Window & { __bespoke?: { play?: () => Promise<void> } };
      await w.__bespoke?.play?.();
    });
    await page.waitForTimeout(800);

    const health = await page.evaluate(() => {
      const w = window as Window & {
        __bespoke?: { getAudioHealth?: () => Record<string, number> | null; getAudioBackend?: () => string };
      };
      return {
        backend: w.__bespoke?.getAudioBackend?.(),
        health: w.__bespoke?.getAudioHealth?.() ?? null,
      };
    });

    expect(health.backend).toBe('worklet');
    expect(health.health).toBeTruthy();
    expect(health.health!.backend).toBe(1);
    expect(health.health!.callbackCount).toBeGreaterThan(0);
    expect(Number.isFinite(health.health!.cpuLoad)).toBe(true);
    expect(health.health!.capacityFrames).toBeGreaterThan(0);
  });

  test('cross-origin isolated fresh load defaults to the worklet', async ({ page }) => {
    await gotoApp(page, 'renderTest=1');
    await waitForBespokeReady(page);

    const choice = await page.evaluate(() => {
      const w = window as Window & {
        __bespoke?: { getAudioBackendChoice?: () => { backend: string; source: string; fallbackReason?: string } };
      };
      return w.__bespoke?.getAudioBackendChoice?.() ?? null;
    });
    expect(choice).toEqual({ backend: 'worklet', requested: 'worklet', source: 'auto' });
    await expect(page.locator('#audioSelect')).toHaveValue('worklet');
  });

  test('without isolation SDL2 starts and the UI explains why', async ({ page }) => {
    await gotoApp(page, 'renderTest=1', '/no-coi/');
    await waitForBespokeReady(page);

    const state = await page.evaluate(() => {
      const w = window as Window & {
        crossOriginIsolated?: boolean;
        __bespoke?: { getAudioBackendChoice?: () => { backend: string; fallbackReason?: string } };
      };
      return { coi: w.crossOriginIsolated === true, choice: w.__bespoke?.getAudioBackendChoice?.() ?? null };
    });
    expect(state.coi).toBe(false);
    expect(state.choice?.backend).toBe('sdl');
    expect(state.choice?.fallbackReason).toBeTruthy();

    const select = page.locator('#audioSelect');
    await expect(select).toHaveValue('sdl');
    await expect(select).toHaveAttribute('title', /AudioWorklet unavailable/);
    await expect(select.locator('option[value="worklet"]')).toBeDisabled();
  });

  test('canonical patch plays 30s on the default worklet path with finite underruns', async ({
    page,
    browserName,
  }) => {
    test.skip(browserName !== 'chromium', 'soak runs on chromium (SwiftShader) only');
    test.setTimeout(90_000);

    await gotoApp(page, 'renderTest=1');
    await waitForBespokeReady(page);

    await page.evaluate(async () => {
      const w = window as Window & { __bespoke?: { play?: () => Promise<void> } };
      await w.__bespoke?.play?.();
    });
    await page.waitForTimeout(30_000);

    const result = await page.evaluate(() => {
      const w = window as Window & {
        __bespoke?: {
          getAudioBackend?: () => string;
          getAudioHealth?: () => Record<string, number> | null;
          getAudioRingStats?: () => Record<string, number> | null;
        };
      };
      return {
        backend: w.__bespoke?.getAudioBackend?.(),
        health: w.__bespoke?.getAudioHealth?.() ?? null,
        ring: w.__bespoke?.getAudioRingStats?.() ?? null,
      };
    });
    console.log('30s soak:', JSON.stringify(result));

    expect(result.backend).toBe('worklet');
    const health = result.health!;
    expect(health.backend).toBe(1);
    expect(Number.isFinite(health.underrunCount)).toBe(true);
    expect(Number.isFinite(health.cpuLoad)).toBe(true);
    // ~30 s of 128-frame blocks at 44.1-48 kHz; allow slack for slow CI.
    expect(health.callbackCount).toBeGreaterThan(5000);
    expect(health.heapBytes).toBeGreaterThan(0);
    expect(result.ring!.lookaheadFrames).toBeGreaterThan(0);
    expect(result.ring!.lookaheadFrames).toBeLessThanOrEqual(result.ring!.capacityFrames);
  });
});
