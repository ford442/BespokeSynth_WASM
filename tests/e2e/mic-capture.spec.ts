import { test, expect } from '@playwright/test';
import { gotoApp, waitForBespokeReady } from './helpers';

// The chromium project launches with a fake capture device (playwright.config.ts)
// that stands in for a real microphone.
test.use({ permissions: ['microphone'] });

interface MicSpy {
  worklets: string[];
  scriptProcessors: number;
  chunks: number;
  chunkFrames: number[];
}

function installMicSpy(): void {
  const spy: MicSpy = { worklets: [], scriptProcessors: 0, chunks: 0, chunkFrames: [] };
  (window as unknown as { __micSpy: MicSpy }).__micSpy = spy;

  const addModule = AudioWorklet.prototype.addModule;
  AudioWorklet.prototype.addModule = function (url: string | URL, options?: WorkletOptions) {
    spy.worklets.push(String(url));
    return addModule.call(this, url, options);
  };

  const createScriptProcessor = BaseAudioContext.prototype.createScriptProcessor;
  BaseAudioContext.prototype.createScriptProcessor = function (...args: Parameters<typeof createScriptProcessor>) {
    spy.scriptProcessors += 1;
    return createScriptProcessor.apply(this, args);
  };

  const NativeNode = window.AudioWorkletNode;
  window.AudioWorkletNode = class extends NativeNode {
    constructor(context: BaseAudioContext, name: string, options?: AudioWorkletNodeOptions) {
      super(context, name, options);
      if (name !== 'bespoke-mic-capture') return;
      this.port.addEventListener('message', (event: MessageEvent<{ type: string; samples?: Float32Array }>) => {
        if (event.data.type !== 'chunk') return;
        spy.chunks += 1;
        spy.chunkFrames.push(event.data.samples?.length ?? 0);
      });
    }
  };
}

test.describe('Mic capture', () => {
  test('streams chunks through an AudioWorklet instead of a ScriptProcessorNode', async ({
    page,
    browserName,
  }) => {
    test.skip(browserName !== 'chromium', 'fake media device flags are Chromium-only');

    await page.addInitScript(installMicSpy);

    await gotoApp(page, 'audio=sdl&renderTest=1');
    await waitForBespokeReady(page);

    const readSpy = () => page.evaluate(() => (window as unknown as { __micSpy: MicSpy }).__micSpy);

    // The SDL2 audio backend may own a ScriptProcessorNode of its own; only mic capture is under test.
    const before = await readSpy();

    await page.locator('#inputCaptureBtn').click();
    await expect.poll(async () => (await readSpy()).chunks, { timeout: 10000 }).toBeGreaterThan(10);

    const running = await readSpy();
    expect(running.worklets.some((url) => url.endsWith('audio/micCapture.worklet.js'))).toBe(true);
    expect(running.scriptProcessors).toBe(before.scriptProcessors);
    expect(new Set(running.chunkFrames)).toEqual(new Set([512]));

    // Toggling off stops the worklet: no further chunks arrive.
    await page.locator('#inputCaptureBtn').click();
    await page.waitForTimeout(200);
    const stopped = (await readSpy()).chunks;
    await page.waitForTimeout(400);
    expect((await readSpy()).chunks).toBe(stopped);
  });

  test('feeds the worklet engine through the shared input ring, never the main thread', async ({
    page,
    browserName,
  }) => {
    test.skip(browserName !== 'chromium', 'fake media device flags are Chromium-only');
    await page.addInitScript(installMicSpy);
    await gotoApp(page, 'audio=worklet');
    await waitForBespokeReady(page);

    // Only Input -> Output, so anything the output plays came through the mic.
    await page.evaluate(() => {
      const api = window.__bespoke!;
      const modules = api.getModules() as Array<{ id: number; type: string }>;
      for (const m of modules) {
        if (!['output', 'transport', 'scale'].includes(m.type)) api.deleteModule(m.id);
      }
      const output = modules.find((m) => m.type === 'output')?.id ?? (api.createModule('output', 400, 200) as number);
      const input = api.createModule('input', 100, 200) as number;
      api.connectModules(input, output);
    });
    await page.locator('#playBtn').click();
    const peak = () =>
      page.evaluate(() => {
        const api = window.__bespoke!;
        return api.getOutputWaveform(2048).reduce((max, v) => Math.max(max, Math.abs(v)), 0);
      });
    await page.waitForTimeout(300);
    expect(await peak()).toBeLessThan(0.001);
    // bespoke_init opens the SDL2 device (a ScriptProcessorNode) whatever the backend; only mic capture is under test.
    const before = await page.evaluate(() => (window as unknown as { __micSpy: MicSpy }).__micSpy.scriptProcessors);

    await page.locator('#inputCaptureBtn').click();
    await expect
      .poll(async () => (await page.evaluate(() => (window as unknown as { __micSpy: MicSpy }).__micSpy)).worklets, {
        timeout: 10000,
      })
      .toContainEqual(expect.stringMatching(/audio\/micCapture\.worklet\.js$/));

    // Chrome's fake capture device beeps periodically; the beep must come out of the engine.
    await expect
      .poll(
        () =>
          peak(),
        { timeout: 10000, intervals: [50] },
      )
      .toBeGreaterThan(0.01);

    const state = await page.evaluate(() => ({
      spy: (window as unknown as { __micSpy: MicSpy }).__micSpy,
      health: window.__bespoke!.getAudioHealth(),
    }));
    expect(state.health!.backend).toBe(3);
    expect(state.spy.chunks).toBe(0);
    expect(state.spy.scriptProcessors).toBe(before);
  });
});
