import { test, expect, type Page } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

/**
 * #126 acceptance on the AudioWorklet engine (Option A): the graph keeps rendering through
 * long main-thread stalls, Script workers keep sending notes during them, and live notes
 * carry start frames. Headless Chromium gives the audio thread no real-time priority, so
 * timing is compared against an idle window measured in the same run, not wall-clock bounds.
 */

/** One idle window followed by one stall, in the same page task as the stall. */
interface StallCycle {
  idleMs: number;
  idleQuanta: number;
  idleXruns: number;
  stallMs: number;
  stallQuanta: number;
  stallXruns: number;
  peak: number;
}

/** Oscillators → output; everything else in the default patch removed (Transport and Scale stay). */
async function setupOscillators(page: Page, count: number): Promise<number[]> {
  return page.evaluate((n) => {
    const api = window.__bespoke!;
    for (const m of api.getModules()) if (m.type !== 'transport' && m.type !== 'scale') api.deleteModule(m.id);
    const out = api.createModule('output', 520, 200);
    const oscs: number[] = [];
    for (let i = 0; i < n; i += 1) {
      const osc = api.createModule('oscillator', 80, 60 + 110 * i);
      api.connectModules(osc, out);
      // Four saws at full volume would only exercise the limiter.
      api.setControlValue(osc, 'volume', 0.08);
      api.setControlValue(osc, 'waveform', 1);
      oscs.push(osc);
    }
    return oscs;
  }, count);
}

test.describe('AudioWorklet engine timing (#126)', () => {
  test.beforeEach(({ browserName }) => {
    test.skip(browserName !== 'chromium', 'engine e2e runs on chromium (SwiftShader) only');
  });

  test('32 voices play through a 500 ms main-thread stall every 2 s', async ({ page }) => {
    test.setTimeout(120_000);
    await gotoApp(page, 'audio=worklet');
    await waitForBespokeReady(page);
    await setupOscillators(page, 4);

    const result = await page.evaluate(async () => {
      const api = window.__bespoke!;
      await api.play();
      // 8 held notes reach every uncabled oscillator: 4 × 8 = 32 voices.
      for (const pitch of [48, 52, 55, 59, 62, 65, 69, 72]) api.midiNoteOn(0, pitch, 0.9);
      await new Promise((resolve) => setTimeout(resolve, 1000));
      const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));
      const cycles: StallCycle[] = [];
      for (let cycle = 0; cycle < 14; cycle += 1) {
        const idleFrom = performance.now();
        const idleStart = api.getAudioHealth()!;
        await sleep(1500);
        const before = api.getAudioHealth()!;
        const idleMs = performance.now() - idleFrom;
        let peak = 0;
        for (const v of api.getOutputWaveform(2048)) peak = Math.max(peak, Math.abs(v));
        const start = performance.now();
        while (performance.now() - start < 500) {
          // A long canvas frame: no rAF, no timers, no message handling.
        }
        const during = api.getAudioHealth()!;
        cycles.push({
          idleMs,
          idleQuanta: before.callbackCount - idleStart.callbackCount,
          idleXruns: before.underrunCount - idleStart.underrunCount,
          stallMs: performance.now() - start,
          stallQuanta: during.callbackCount - before.callbackCount,
          stallXruns: during.underrunCount - before.underrunCount,
          peak,
        });
      }
      await sleep(300);
      return { backend: api.getAudioBackend(), sampleRate: api.getSampleRate(), health: api.getAudioHealth()!, cycles };
    });

    const sum = (key: keyof StallCycle) => result.cycles.reduce((total, c) => total + c[key], 0);
    const idleRate = sum('idleQuanta') / sum('idleMs');
    const stallRate = sum('stallQuanta') / sum('stallMs');
    const realRate = result.sampleRate / 128 / 1000;
    console.log(
      `32 voices, ${result.cycles.length} × 500 ms stalls: ` +
        `${(stallRate / realRate).toFixed(2)}× real time while stalled, ${(idleRate / realRate).toFixed(2)}× idle; ` +
        `xruns ${sum('stallXruns')} in ${sum('stallMs').toFixed(0)} ms stalled, ${sum('idleXruns')} in ${sum('idleMs').toFixed(0)} ms idle; ` +
        `histogram ${JSON.stringify(result.health.blockTimeHistogram)}, max ${result.health.maxProcessTimeMs.toFixed(2)} ms, ` +
        `rt locks ${result.health.rtLocks}, scope ${result.health.engineScope}`,
    );

    expect(result.backend).toBe('worklet');
    const health = result.health;
    expect(health.engineError).toBeUndefined();
    expect(health.mainThreadBlocks).toBe(0);
    // Blocks ran in AudioWorkletGlobalScope (the Debug build aborts otherwise).
    expect(health.engineScope).toBe(3);
    expect(health.rtLocks).toBe(0);
    // Every stall window kept rendering, at least at half the unstalled rate of this machine.
    for (const cycle of result.cycles) expect(cycle.stallQuanta).toBeGreaterThan(0);
    expect(stallRate).toBeGreaterThanOrEqual(idleRate * 0.5);
    // Stalls add no xruns beyond the machine's background rate (headless preemption), with slack.
    const expectedStallXruns = (sum('idleXruns') / sum('idleMs')) * sum('stallMs');
    expect(sum('stallXruns')).toBeLessThanOrEqual(Math.ceil(expectedStallXruns * 2) + 2);
    // Still sounding after a dozen stalls: no voice was dropped.
    expect(result.cycles.at(-1)!.peak).toBeGreaterThan(0.05);
    const histogram = health.blockTimeHistogram ?? [];
    expect(histogram.reduce((a, b) => a + b, 0)).toBe(health.callbackCount);
  });

  test('Script notes keep coming from the worker while the main thread is stalled', async ({ page }) => {
    test.setTimeout(60_000);
    await gotoApp(page, 'audio=worklet&script=1');
    await waitForBespokeReady(page);
    await setupOscillators(page, 1);

    const result = await page.evaluate(async () => {
      const api = window.__bespoke!;
      await api.play();
      api.runScript(`bespoke.onBeat((beat) => bespoke.noteOn(60 + (beat % 12), 0.8, 120));`);
      await new Promise((resolve) => setTimeout(resolve, 1500));
      const before = api.getAudioHealth()!;
      const start = performance.now();
      while (performance.now() - start < 2000) {
        // Main thread blocked: a relayed script would send nothing until this ends.
      }
      const during = api.getAudioHealth()!;
      return { before, during, stallMs: performance.now() - start, status: api.getScriptStatus() };
    });
    console.log(
      `script during a ${result.stallMs.toFixed(0)} ms stall: direct notes ${result.before.directScriptNotes} → ` +
        `${result.during.directScriptNotes}, timed ${result.during.timedNotes}, late ${result.during.lateNotes}`,
    );

    expect(result.status).toBe('running');
    expect(result.before.directScriptNotes).toBeGreaterThan(0);
    // 120 BPM: about four beats in 2 s, each a note-on and its note-off. Allow for a slow worker.
    expect(result.during.directScriptNotes! - result.before.directScriptNotes!).toBeGreaterThanOrEqual(4);
    expect(result.during.timedNotes!).toBeGreaterThan(result.before.timedNotes!);
  });

  test('QWERTY notes reach the engine with start frames', async ({ page }) => {
    await gotoApp(page, 'audio=worklet');
    await waitForBespokeReady(page);
    await setupOscillators(page, 1);
    await page.evaluate(() => window.__bespoke!.play());
    const before = await page.evaluate(() => window.__bespoke!.getAudioHealth()!);

    await page.locator('#canvas').focus();
    const keys = ['z', 'x', 'c', 'v', 'b', 'n'];
    for (const key of keys) {
      await page.keyboard.down(key);
      await page.waitForTimeout(40);
      await page.keyboard.up(key);
      await page.waitForTimeout(30);
    }
    await page.waitForTimeout(200);
    const after = await page.evaluate(() => window.__bespoke!.getAudioHealth()!);
    console.log(
      `QWERTY: timed +${after.timedNotes! - before.timedNotes!}, late +${after.lateNotes! - before.lateNotes!}, ` +
        `split blocks +${after.splitBlocks! - before.splitBlocks!}, margin ${after.noteMarginMs?.toFixed(1)} ms`,
    );

    // Every note carried a start frame. Where it lands inside a quantum (splitBlocks) depends on
    // how long headless Chromium holds input events, so that is only logged.
    expect(after.timedNotes! - before.timedNotes!).toBe(keys.length * 2);
    expect(after.noteMarginMs).toBeGreaterThan(0);
  });
});
