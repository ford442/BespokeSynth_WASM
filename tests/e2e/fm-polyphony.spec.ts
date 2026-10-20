import { test, expect, type Page } from '@playwright/test';
import { AUDIO_BACKENDS, gotoApp, waitForBespokeReady } from './helpers';

// #127: the 8-voice FM synth plays chords from the computer keyboard and from Web MIDI,
// and steals the oldest voice past its limit.

const C4 = 261.63;
const E4 = 329.63;
const G4 = 392.0;

/** Replace the default patch with fmsynth → output, tuned to pure sines (no modulation). */
async function setupFm(page: Page, voices: number): Promise<number> {
  return page.evaluate((voiceCount) => {
    const api = window.__bespoke!;
    for (const m of api.getModules() as Array<{ id: number; type: string }>) {
      if (m.type !== 'transport' && m.type !== 'scale') api.deleteModule(m.id);
    }
    const fm = api.createModule('fmsynth', 120, 160) as number;
    const out = api.createModule('output', 460, 160) as number;
    api.connectModules(fm, out);
    for (const [name, value] of Object.entries({
      harmratio: 5, mod: 0, mod2: 0, attack: 1, decay: 1, sustain: 1, release: 20, voices: voiceCount,
    })) {
      api.setControlValue(fm, name, value);
    }
    return fm;
  }, voices);
}

/** Amplitude of each frequency in the latest output (Hann-windowed Goertzel). */
async function toneLevels(page: Page, freqs: number[]): Promise<number[]> {
  return page.evaluate((targets) => {
    const api = window.__bespoke!;
    const samples = api.getOutputWaveform(2048);
    const rate = api.getSampleRate();
    const n = samples.length;
    return targets.map((freq) => {
      const w = (2 * Math.PI * freq) / rate;
      let re = 0;
      let im = 0;
      for (let i = 0; i < n; i++) {
        const hann = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (n - 1));
        re += samples[i] * hann * Math.cos(w * i);
        im -= samples[i] * hann * Math.sin(w * i);
      }
      return (2 * Math.hypot(re, im)) / (n * 0.5);
    });
  }, freqs);
}

test.describe('FM synth polyphony', () => {
  for (const audio of AUDIO_BACKENDS) {
    test(`QWERTY chord sounds all three notes [${audio}]`, async ({ page }) => {
      await gotoApp(page, `audio=${audio}`);
      await waitForBespokeReady(page);
      await setupFm(page, 8);
      await page.locator('#playBtn').click();
      await page.locator('#canvas').focus();

      // Z, C, B are C4, E4, G4 (all in the default C major Scale).
      for (const key of ['z', 'c', 'b']) await page.keyboard.down(key);
      await page.waitForTimeout(500);
      const [c, e, g] = await toneLevels(page, [C4, E4, G4]);
      for (const key of ['z', 'c', 'b']) await page.keyboard.up(key);

      for (const level of [c, e, g]) expect(level).toBeGreaterThan(0.01);
      expect(Math.min(c, e, g) / Math.max(c, e, g)).toBeGreaterThan(0.3);
    });
  }

  test('a third note steals the oldest of two voices', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);
    await setupFm(page, 2);
    await page.locator('#playBtn').click();
    await page.locator('#canvas').focus();

    await page.keyboard.down('z');
    await page.waitForTimeout(120);
    await page.keyboard.down('c');
    await page.waitForTimeout(120);
    await page.keyboard.down('b');
    await page.waitForTimeout(500);
    const [c, e, g] = await toneLevels(page, [C4, E4, G4]);
    for (const key of ['z', 'c', 'b']) await page.keyboard.up(key);

    expect(e).toBeGreaterThan(0.01);
    expect(g).toBeGreaterThan(0.01);
    expect(c).toBeLessThan(0.2 * Math.min(e, g));
  });

  test('Web MIDI input chord sounds all three notes', async ({ page }) => {
    await page.addInitScript(() => {
      const input = {
        id: 'keys-1',
        name: 'Virtual Keys',
        state: 'connected',
        onmidimessage: null as ((event: { data: Uint8Array; timeStamp: number }) => void) | null,
      };
      (window as unknown as { __fakeMidiInput: typeof input }).__fakeMidiInput = input;
      const access = Object.assign(new EventTarget(), {
        inputs: new Map([[input.id, input]]),
        outputs: new Map(),
      });
      Object.defineProperty(navigator, 'requestMIDIAccess', {
        configurable: true,
        value: () => Promise.resolve(access),
      });
    });
    await gotoApp(page);
    await waitForBespokeReady(page);
    await setupFm(page, 8);
    await page.locator('#playBtn').click();
    await page.locator('#midiBtn').click();
    await expect
      .poll(() => page.evaluate(() => (window as unknown as { __fakeMidiInput: { onmidimessage: unknown } })
        .__fakeMidiInput.onmidimessage !== null))
      .toBe(true);

    const send = (bytes: number[]) =>
      page.evaluate((data) => {
        const input = (window as unknown as {
          __fakeMidiInput: { onmidimessage: (event: { data: Uint8Array; timeStamp: number }) => void };
        }).__fakeMidiInput;
        input.onmidimessage({ data: new Uint8Array(data), timeStamp: performance.now() });
      }, bytes);
    for (const pitch of [60, 64, 67]) await send([0x90, pitch, 100]);
    await page.waitForTimeout(500);
    const [c, e, g] = await toneLevels(page, [C4, E4, G4]);
    for (const pitch of [60, 64, 67]) await send([0x80, pitch, 0]);

    for (const level of [c, e, g]) expect(level).toBeGreaterThan(0.01);
  });
});
