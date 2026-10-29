import { test, expect, type Page } from '@playwright/test';
import { AUDIO_BACKENDS, gotoApp, waitForBespokeReady } from './helpers';

type Sent = { bytes: number[]; timestamp: number | undefined; sentAt: number };

/** A loopback MIDI output the page can select; records every message with its timestamp. */
async function stubMidiOutput(page: Page): Promise<void> {
  await page.addInitScript(() => {
    const sent: Sent[] = [];
    (window as unknown as { __fakeMidi: Sent[] }).__fakeMidi = sent;
    const output = {
      id: 'loopback-1',
      name: 'Virtual Loopback',
      send: (data: number[], timestamp?: number) =>
        sent.push({ bytes: [...data], timestamp, sentAt: performance.now() }),
    };
    const access = Object.assign(new EventTarget(), {
      inputs: new Map(),
      outputs: new Map([[output.id, output]]),
    });
    Object.defineProperty(navigator, 'requestMIDIAccess', {
      configurable: true,
      value: () => Promise.resolve(access),
    });
  });
}

async function selectLoopback(page: Page): Promise<void> {
  await page.locator('#midiBtn').click();
  await expect(page.locator('#midiOutputSelect option[value="loopback-1"]')).toHaveCount(1);
  await page.locator('#midiOutputSelect').selectOption('loopback-1');
  await page.locator('#midiOutputSelect').blur();
}

async function midiSent(page: Page): Promise<Sent[]> {
  return page.evaluate(() =>
    (window as unknown as { __fakeMidi: Sent[] }).__fakeMidi.map((m) => ({
      ...m,
    })),
  );
}

const noteOns = (sent: Sent[]) => sent.filter((m) => (m.bytes[0] & 0xf0) === 0x90).map((m) => m.bytes[1]);
const noteOffs = (sent: Sent[]) => sent.filter((m) => (m.bytes[0] & 0xf0) === 0x80).map((m) => m.bytes[1]);

function patchJson(
  modules: Array<{
    id: number;
    type: string;
    controls?: Record<string, number>;
    extras?: Record<string, string>;
  }>,
  cables: Array<[number, number, number, number]>,
): string {
  return JSON.stringify({
    filetype: 'bespokesynth',
    format: 'bespokesynth-wasm-state',
    version: 4,
    transport: { bpm: 120, playing: false },
    modules: modules.map((m, i) => ({
      id: m.id,
      type: m.type,
      position: { x: 40 + (i % 4) * 190, y: 60 + Math.floor(i / 4) * 150 },
      controls: m.controls ?? {},
      ...(m.extras ? { extras: m.extras } : {}),
    })),
    connections: cables.map(([src, srcPort, dst, dstPort]) => ({
      source: { module: src, port: srcPort },
      destination: { module: dst, port: dstPort },
    })),
  });
}

async function loadPatch(page: Page, json: string): Promise<boolean> {
  return page.evaluate(
    (text) =>
      window.__bespoke!.loadStateJson(text),
    json,
  );
}

async function outputRms(page: Page, count = 2048): Promise<number> {
  return page.evaluate((n) => {
    const samples = window.__bespoke!.getOutputWaveform(n);
    let sum = 0;
    for (const v of samples) sum += v * v;
    return Math.sqrt(sum / Math.max(1, samples.length));
  }, count);
}

test.describe('Wave 3: note graph, kit, placeholders', () => {
  test('spawn menu lists the wave-3 modules under a Note chip', async ({ page }) => {
    await gotoApp(page, 'audio=sdl');
    await waitForBespokeReady(page);
    const types = await page.evaluate(() => {
      const api = window.__bespoke!;
      const created = ['input', 'eq', 'chorder', 'arpeggiator', 'notefilter', 'noterouter'].map(
        (type) => [type, api.createModule(type, 60, 60)] as const,
      );
      const modules = api.getModules();
      return created.map(([type, id]) => ({
        type,
        id,
        present: modules.some((m) => m.id === id && m.type === type),
      }));
    });
    for (const entry of types) {
      expect(entry.id, `${entry.type} spawns`).toBeGreaterThan(0);
      expect(entry.present, `${entry.type} is in the patch`).toBe(true);
    }
  });

  for (const audio of AUDIO_BACKENDS) {
    test(`MIDI in -> chorder -> oscillator plays a chord, and MIDI out carries it once [${audio}]`, async ({
      page,
    }) => {
      await stubMidiOutput(page);
      await gotoApp(page, `audio=${audio}`);
      await waitForBespokeReady(page);
      await selectLoopback(page);

      expect(
        await loadPatch(
          page,
          patchJson(
            [
              {
                id: 10,
                type: 'oscillator',
                controls: { volume: 1, waveform: 0, frequency: 220 },
              },
              { id: 11, type: 'chorder', controls: { chord: 0 } },
              { id: 12, type: 'output', controls: { level: 0.8 } },
            ],
            [
              [11, 0, 10, 0],
              [10, 0, 12, 0],
            ],
          ),
        ),
      ).toBe(true);
      await page.locator('#playBtn').click();
      await page.locator('#canvas').focus();

      await page.keyboard.down('z'); // C4
      await page.waitForTimeout(500);
      expect(await outputRms(page)).toBeGreaterThan(0.02);
      const held = await midiSent(page);
      expect(noteOns(held).sort((a, b) => a - b)).toEqual([60, 64, 67]); // the chord, with no raw 60 as well
      await page.keyboard.up('z');
      await page.waitForTimeout(300);
      const released = await midiSent(page);
      expect(noteOffs(released).sort((a, b) => a - b)).toEqual([60, 64, 67]);
    });
  }

  for (const audio of AUDIO_BACKENDS) {
    test(`arpeggiator plays a pattern from a held chord with no further input [${audio}]`, async ({ page }) => {
      await stubMidiOutput(page);
      await gotoApp(page, `audio=${audio}`);
      await waitForBespokeReady(page);
      await selectLoopback(page);

      await loadPatch(
        page,
        patchJson(
          [
            { id: 10, type: 'oscillator', controls: { volume: 1, waveform: 0 } },
            {
              id: 11,
              type: 'arpeggiator',
              controls: { rate: 2, mode: 0, octaves: 1, gate: 0.6 },
            },
            { id: 12, type: 'output', controls: { level: 0.8 } },
          ],
          [
            [11, 0, 10, 0],
            [10, 0, 12, 0],
          ],
        ),
      );
      await page.evaluate(() => {
        const api = window.__bespoke!;
        for (const pitch of [60, 64, 67]) api.midiNoteOn(0, pitch, 0.9);
      });
      await page.locator('#playBtn').click();
      // 120 BPM: a sixteenth every 125 ms, so ~0.75 s once audio runs (the worklet engine starts on Play).
      await expect.poll(async () => noteOns(await midiSent(page)).length, { timeout: 5000 }).toBeGreaterThanOrEqual(6);
      const ons = noteOns(await midiSent(page));
      ons.slice(0, 6).forEach((pitch, i) => expect(pitch).toBe([60, 64, 67][i % 3]));
      // The scope holds ~46 ms, less than the ~50 ms gap between gated sixteenths, so one read can be silent.
      await expect.poll(() => outputRms(page, 4096), { timeout: 2000, intervals: [30] }).toBeGreaterThan(0.005);
    });
  }

  test('?patch=drumkit plays the bundled kit and its notes reach the MIDI port', async ({ page }) => {
    await stubMidiOutput(page);
    await gotoApp(page, 'patch=drumkit');
    await waitForBespokeReady(page);
    expect(
      await page.evaluate(() =>
        window.__bespoke!.getPlaceholderCount(),
      ),
    ).toBe(0);

    const level = await page.evaluate(() => {
      const wav = window.__bespoke!.renderOfflineWav({
        seconds: 2,
        bitsPerSample: 16,
      });
      if (!wav) return null;
      const view = new DataView(wav.buffer, wav.byteOffset, wav.byteLength);
      let offset = 12;
      while (offset + 8 <= view.byteLength) {
        const id = String.fromCharCode(
          view.getUint8(offset),
          view.getUint8(offset + 1),
          view.getUint8(offset + 2),
          view.getUint8(offset + 3),
        );
        const size = view.getUint32(offset + 4, true);
        if (id === 'data') {
          let sum = 0;
          const samples = Math.floor(size / 2);
          for (let i = 0; i < samples; i++) {
            const v = view.getInt16(offset + 8 + i * 2, true) / 32768;
            sum += v * v;
          }
          return Math.sqrt(sum / Math.max(1, samples));
        }
        offset += 8 + size + (size % 2);
      }
      return null;
    });
    expect(level).not.toBeNull();
    expect(level!).toBeGreaterThan(0.02);

    await selectLoopback(page);
    await page.locator('#playBtn').click();
    await page.waitForTimeout(1500);
    const sent = await midiSent(page);
    const ons = new Set(noteOns(sent));
    expect(ons.has(36)).toBe(true); // kick lane
    expect(ons.has(42)).toBe(true); // hat lane
    // Graph notes are scheduled for when the audio is heard, never in the past.
    const stamped = sent.filter((m) => m.timestamp !== undefined);
    expect(stamped.length).toBeGreaterThan(0);
    for (const m of stamped) expect(m.timestamp!).toBeGreaterThanOrEqual(m.sentAt - 1);
  });

  test('a patch with an unknown module type loads a placeholder and saves it back', async ({ page }) => {
    await gotoApp(page, 'audio=sdl');
    await waitForBespokeReady(page);
    expect(
      await loadPatch(
        page,
        patchJson(
          [
            { id: 1, type: 'oscillator', controls: { volume: 0.5 } },
            {
              id: 2,
              type: 'multitapdelay', // desktop-only, no WASM adapter
              controls: { depth: 0.7, bands: 16 },
              extras: { preset: 'robot' },
            },
            { id: 3, type: 'output' },
          ],
          [
            [1, 0, 2, 0],
            [2, 0, 3, 0],
            [1, 0, 3, 0],
          ],
        ),
      ),
    ).toBe(true);

    const result = await page.evaluate(() => {
      const api = window.__bespoke!;
      return {
        count: api.getPlaceholderCount(),
        state: JSON.parse(api.getStateJson()),
      };
    });
    expect(result.count).toBe(1);
    const unknown = result.state.modules.find((m: { type: string }) => m.type === 'multitapdelay');
    expect(unknown).toBeTruthy();
    expect(unknown.controls.depth).toBeCloseTo(0.7);
    expect(unknown.extras.preset).toBe('robot');
    expect(result.state.modules.some((m: { type: string }) => m.type === 'placeholder')).toBe(false);
    expect(result.state.connections).toHaveLength(3); // the cables through the unknown module are kept
  });
});
