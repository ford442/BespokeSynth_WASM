import { test, expect, type Page } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

type Health = Record<string, number> & { engineError?: string };

interface StallResult {
  backend: number | undefined;
  sampleRate: number;
  /** Health at the start of an idle window as long as the stall, which ends at `before`. */
  idleStart: Health;
  idleMs: number;
  stallMs: number;
  before: Health;
  during: Health;
  after: Health;
  ringBefore: Record<string, number> | null;
  ringAfter: Record<string, number> | null;
}

/**
 * Plays, lets the backend settle, then blocks the main thread for `stallMs` the way a long
 * canvas frame would. `during` is read in the same task, right as the stall ends. An idle window
 * of the same length just before the stall gives the backend's unstalled rate on this machine.
 */
async function stallMainThread(page: Page, stallMs: number): Promise<StallResult> {
  return page.evaluate(async (stall) => {
    const api = (
      window as unknown as {
        __bespoke: {
          play: () => Promise<void>;
          getAudioHealth: () => Health | null;
          getAudioRingStats: () => Record<string, number> | null;
          getSampleRate: () => number;
        };
      }
    ).__bespoke;
    await api.play();
    await new Promise((resolve) => setTimeout(resolve, 1400));
    const idleStart = api.getAudioHealth()!;
    const idleFrom = performance.now();
    await new Promise((resolve) => setTimeout(resolve, stall));
    const before = api.getAudioHealth()!;
    const ringBefore = api.getAudioRingStats();
    const start = performance.now();
    while (performance.now() - start < stall) {
      // Busy: no rAF, no timers, no producer ticks.
    }
    const during = api.getAudioHealth()!;
    const stallMs = performance.now() - start;
    await new Promise((resolve) => setTimeout(resolve, 500));
    return {
      backend: before.backend,
      sampleRate: api.getSampleRate(),
      idleStart,
      idleMs: start - idleFrom,
      stallMs,
      before,
      during,
      after: api.getAudioHealth()!,
      ringBefore,
      ringAfter: api.getAudioRingStats(),
    };
  }, stallMs);
}

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

  test('worklet SAB ring path (main-thread DSP) initializes and plays canonical patch', async ({ page }) => {
    await gotoApp(page, 'audio=ring&renderTest=1');
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

    expect(health.backend).toBe('ring');
    expect(health.health).toBeTruthy();
    expect(health.health!.backend).toBe(1);
    expect(health.health!.callbackCount).toBeGreaterThan(0);
    expect(Number.isFinite(health.health!.cpuLoad)).toBe(true);
    expect(health.health!.capacityFrames).toBeGreaterThan(0);
  });

  test('worklet engine runs the graph in the AudioWorklet, not on the main thread', async ({ page, browserName }) => {
    test.skip(browserName !== 'chromium', 'engine e2e runs on chromium (SwiftShader) only');
    await gotoApp(page, 'audio=worklet&renderTest=1');
    await waitForBespokeReady(page);

    const result = await page.evaluate(async () => {
      const api = (
        window as unknown as {
          __bespoke: {
            play: () => Promise<void>;
            getAudioBackend: () => string;
            getAudioHealth: () => Health | null;
            getTransportBeat: () => number;
            getOutputWaveform: (count?: number) => number[];
          };
        }
      ).__bespoke;
      await api.play();
      await new Promise((resolve) => setTimeout(resolve, 500));
      const beatA = api.getTransportBeat();
      // The sequencer gates 4 of 16 steps, so watch the scope for a whole bar (~1.9 s at 128 BPM).
      let peak = 0;
      for (let i = 0; i < 40; i += 1) {
        await new Promise((resolve) => setTimeout(resolve, 50));
        for (const v of api.getOutputWaveform(2048)) peak = Math.max(peak, Math.abs(v));
      }
      return {
        backend: api.getAudioBackend(),
        health: api.getAudioHealth(),
        beatA,
        beatB: api.getTransportBeat(),
        peak,
      };
    });
    console.log('worklet engine:', JSON.stringify(result.health));

    expect(result.backend).toBe('worklet');
    const health = result.health!;
    expect(health.engineError).toBeUndefined();
    expect(health.backend).toBe(3);
    expect(health.callbackCount).toBeGreaterThan(50);
    // The UI module rendered no audio itself: processBlock ran only in the worklet.
    expect(health.mainThreadBlocks).toBe(0);
    expect(health.snapshotCount).toBeGreaterThanOrEqual(1);
    expect(health.audioHeapBytes).toBeGreaterThan(0);
    expect(health.heapBytes).toBeGreaterThan(0);
    expect(result.beatB).toBeGreaterThan(result.beatA);
    expect(result.peak).toBeGreaterThan(0.01);
  });

  test('a 100 ms main-thread stall does not reach the worklet engine', async ({ page, browserName }) => {
    test.skip(browserName !== 'chromium', 'engine e2e runs on chromium (SwiftShader) only');
    await gotoApp(page, 'audio=worklet&renderTest=1');
    await waitForBespokeReady(page);

    const r = await stallMainThread(page, 100);
    const quanta = (r.sampleRate * (r.stallMs / 1000)) / 128;
    const rendered = r.during.callbackCount - r.before.callbackCount;
    const idleRendered = r.before.callbackCount - r.idleStart.callbackCount;
    // What this machine renders unstalled in the same time; with parallel workers sharing the CPU
    // (SwiftShader is heavy) that can be well under real time.
    const baseline = Math.min(quanta, (idleRendered * r.stallMs) / Math.max(1, r.idleMs));
    const stallUnderruns = r.during.underrunCount - r.before.underrunCount;
    console.log(
      `worklet engine stall: ${rendered} quanta rendered during ${r.stallMs.toFixed(0)} ms ` +
        `(~${quanta.toFixed(0)} real time, ${idleRendered} in the ${r.idleMs.toFixed(0)} ms idle window before), ` +
        `underruns +${stallUnderruns} during / +${r.after.underrunCount - r.before.underrunCount} by +500 ms, ` +
        `max quantum ${r.after.maxProcessTimeMs} ms, output latency ${r.ringAfter?.outputLatencyMs?.toFixed(1)} ms`,
    );
    expect(r.backend).toBe(3);
    // The worklet kept rendering while the main thread was blocked: a stall that reached it would
    // leave the whole window unrendered. Half the unstalled rate allows for headless thread preemption.
    expect(rendered).toBeGreaterThan(0);
    expect(rendered).toBeGreaterThanOrEqual(Math.floor(baseline * 0.5));
    // underrunCount is "one quantum took longer than its own duration". Headless Chromium does not
    // give the audio thread real-time priority, so SwiftShader can preempt it for a quantum now and
    // then regardless of the stall (the 30 s soak logs the background rate).
    expect(stallUnderruns).toBeLessThanOrEqual(1);
    expect(r.after.mainThreadBlocks).toBe(0);
  });

  test('the same stall on the main-thread ring is recorded for comparison', async ({ page, browserName }) => {
    test.skip(browserName !== 'chromium', 'engine e2e runs on chromium (SwiftShader) only');
    await gotoApp(page, 'audio=ring&renderTest=1');
    await waitForBespokeReady(page);

    const r = await stallMainThread(page, 100);
    const underrunFrames = (r.ringAfter?.underrunFrames ?? 0) - (r.ringBefore?.underrunFrames ?? 0);
    console.log(
      `ring stall: lookahead ${r.ringBefore?.lookaheadFrames} frames ` +
        `(${(((r.ringBefore?.lookaheadFrames ?? 0) / r.sampleRate) * 1000).toFixed(0)} ms), ` +
        `underrun frames +${underrunFrames}`,
    );
    expect(r.backend).toBe(1);
    expect(Number.isFinite(underrunFrames)).toBe(true);
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
    await expect(select.locator('option[value="ring"]')).toBeDisabled();
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
    expect(health.backend).toBe(3);
    expect(health.mainThreadBlocks).toBe(0);
    expect(Number.isFinite(health.underrunCount)).toBe(true);
    expect(Number.isFinite(health.cpuLoad)).toBe(true);
    // ~30 s of 128-frame blocks at 44.1-48 kHz; allow slack for slow CI.
    expect(health.callbackCount).toBeGreaterThan(5000);
    expect(health.heapBytes).toBeGreaterThan(0);
    // One render quantum ahead of the device, versus the ring's adaptive 20-500 ms.
    expect(result.ring!.lookaheadFrames).toBe(128);
    expect(result.ring!.lookaheadFrames).toBeLessThanOrEqual(result.ring!.capacityFrames);
  });
});
