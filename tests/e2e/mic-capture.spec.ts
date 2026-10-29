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

test.describe('Mic capture', () => {
  test('streams chunks through an AudioWorklet instead of a ScriptProcessorNode', async ({
    page,
    browserName,
  }) => {
    test.skip(browserName !== 'chromium', 'fake media device flags are Chromium-only');

    await page.addInitScript(() => {
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
    });

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
});
