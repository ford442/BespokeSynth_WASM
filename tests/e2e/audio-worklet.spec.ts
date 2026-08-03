import { test, expect } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

test.describe('Audio worklet path', () => {
  test('exposes audio health metrics on SDL default path', async ({ page }) => {
    await gotoApp(page, 'renderTest=1');
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
});
