import { test, expect, type Page } from '@playwright/test';
import { AUDIO_BACKENDS, gotoApp, waitForBespokeReady } from './helpers';

/** Loose view of window.__bespoke for page.evaluate callbacks. */
type Api = Record<string, (...args: never[]) => unknown>;

const JUST_MAJOR_SCL = [
  '! just_major.scl',
  'Just intonation major',
  ' 7',
  '!',
  ' 9/8',
  ' 5/4',
  ' 4/3',
  ' 3/2',
  ' 5/3',
  ' 15/8',
  ' 2/1',
  '',
].join('\n');

async function canvasBox(page: Page) {
  const box = await page.locator('#canvas').boundingBox();
  if (!box) throw new Error('canvas has no box');
  return box;
}

async function moduleInfo(page: Page, id: number) {
  return page.evaluate((moduleId) => {
    const api = (window as unknown as { __bespoke: { getModuleInfo: (id: number) => Record<string, number> } }).__bespoke;
    return api.getModuleInfo(moduleId);
  }, id);
}

/** Replace the default patch with oscillator → output (osc Pitch input left free for MIDI/QWERTY). */
async function setupBareOscillator(page: Page): Promise<{ osc: number; out: number; scale: number }> {
  return page.evaluate(() => {
    const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
    const modules = api.getModules() as Array<{ id: number; type: string }>;
    for (const m of modules) if (m.type !== 'transport' && m.type !== 'scale') api.deleteModule(m.id);
    const osc = api.createModule('oscillator', 120, 160) as number;
    const out = api.createModule('output', 420, 160) as number;
    api.connectModules(osc, out);
    api.setControlValue(osc, 'volume', 1);
    api.setControlValue(osc, 'waveform', 0);
    const scale = (api.getModules() as Array<{ id: number; type: string }>).find((m) => m.type === 'scale')!.id;
    return { osc, out, scale };
  });
}

/** Dominant frequency of the latest output via positive zero crossings. */
async function measuredOutputHz(page: Page): Promise<number> {
  return page.evaluate(() => {
    const w = window as unknown as { __bespoke: { getOutputWaveform: (n: number) => number[] } };
    const samples = w.__bespoke.getOutputWaveform(2048);
    const crossings: number[] = [];
    for (let i = 1; i < samples.length; i++) {
      if (samples[i - 1] <= 0 && samples[i] > 0) {
        const t = samples[i - 1] / (samples[i - 1] - samples[i]);
        crossings.push(i - 1 + t);
      }
    }
    if (crossings.length < 3) return 0;
    const periodSamples = (crossings[crossings.length - 1] - crossings[0]) / (crossings.length - 1);
    const rate = (window as unknown as { __bespoke: { getSampleRate: () => number } }).__bespoke.getSampleRate();
    return rate / periodSamples;
  });
}

test.describe('Live instrument', () => {
  test('MIDI learn: Shift-click filter cutoff, learned CC survives a patch reload', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);

    const filter = await page.evaluate(() => {
      const api = (window as unknown as { __bespoke: Api }).__bespoke as unknown as {
        getModules: () => Array<{ id: number; type: string }>;
      };
      return api.getModules().find((m) => m.type === 'filter')!.id;
    });
    const info = await moduleInfo(page, filter);
    const box = await canvasBox(page);
    // Filter text lines start 18 px below its 18 px title bar; line 1 (15 px tall) is cutoff.
    const x = box.x + Number(info.screenX) + 30 * Number(info.scale);
    const y = box.y + Number(info.screenY) + (36 + 15 * 1.5) * Number(info.scale);

    await page.keyboard.down('Shift');
    await page.mouse.click(x, y);
    await page.keyboard.up('Shift');

    const target = await page.evaluate(() =>
      (window as unknown as { __bespoke: { getMidiLearnTarget: () => unknown } }).__bespoke.getMidiLearnTarget(),
    );
    expect(target).toEqual({ moduleId: filter, control: 'cutoff' });

    const afterLearn = await page.evaluate((id) => {
      const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
      api.midiCc(1, 21, 0.5);
      return {
        cutoff: api.getControlValue(id, 'cutoff') as number,
        armed: api.getMidiLearnTarget(),
        map: api.getMidiCcMap() as Array<{ cc: number; channel: number; moduleId: number; control: string }>,
        json: api.getStateJson() as string,
      };
    }, filter);
    expect(afterLearn.armed).toBeNull();
    expect(afterLearn.cutoff).toBeCloseTo(10010, 0);
    expect(afterLearn.map).toContainEqual(expect.objectContaining({ cc: 21, channel: 1, moduleId: filter, control: 'cutoff' }));

    await page.reload();
    await waitForBespokeReady(page);
    const reloaded = await page.evaluate((json) => {
      const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
      api.loadStateJson(json);
      const id = (api.getModules() as Array<{ id: number; type: string }>).find((m) => m.type === 'filter')!.id;
      api.midiCc(1, 21, 1);
      const full = api.getControlValue(id, 'cutoff') as number;
      api.midiCc(2, 21, 0); // different channel: ignored
      const otherChannel = api.getControlValue(id, 'cutoff') as number;
      return { full, otherChannel };
    }, afterLearn.json);
    expect(reloaded.full).toBeCloseTo(20000, 0);
    expect(reloaded.otherChannel).toBeCloseTo(20000, 0);
  });

  for (const audio of AUDIO_BACKENDS) {
    test(`QWERTY piano plays the oscillator through Scale quantization [${audio}]`, async ({ page }) => {
      await gotoApp(page, `audio=${audio}`);
      await waitForBespokeReady(page);
      const { scale } = await setupBareOscillator(page);
      await page.locator('#playBtn').click();

      // C major: the S key (C#4) snaps up to D4.
      await page.evaluate((id) => {
        const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
        api.setControlValue(id, 'root', 0);
        api.setControlValue(id, 'type', 0);
      }, scale);
      await page.locator('#canvas').focus();
      await page.keyboard.down('s');
      await page.waitForTimeout(500);
      const major = await measuredOutputHz(page);
      await page.keyboard.up('s');

      // Chromatic: the same key is C#4.
      await page.evaluate((id) => {
        const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
        api.setControlValue(id, 'type', 6);
      }, scale);
      await page.waitForTimeout(150);
      await page.keyboard.down('s');
      await page.waitForTimeout(500);
      const chromatic = await measuredOutputHz(page);
      await page.keyboard.up('s');

      expect(Math.abs(major - 293.66)).toBeLessThan(6);
      expect(Math.abs(chromatic - 277.18)).toBeLessThan(6);
    });
  }

  test('MIDI output: selected port receives note-on from the computer keyboard', async ({ page }) => {
    await page.addInitScript(() => {
      const sent: number[][] = [];
      (window as unknown as { __fakeMidiSent: number[][] }).__fakeMidiSent = sent;
      const output = { id: 'loopback-1', name: 'Virtual Loopback', send: (data: number[]) => sent.push([...data]) };
      const access = Object.assign(new EventTarget(), {
        inputs: new Map(),
        outputs: new Map([[output.id, output]]),
      });
      Object.defineProperty(navigator, 'requestMIDIAccess', {
        configurable: true,
        value: () => Promise.resolve(access),
      });
    });
    await gotoApp(page);
    await waitForBespokeReady(page);

    await page.locator('#midiBtn').click();
    await expect(page.locator('#midiOutputSelect option[value="loopback-1"]')).toHaveCount(1);
    await page.locator('#midiOutputSelect').selectOption('loopback-1');
    await page.locator('#midiOutputSelect').blur();

    await page.keyboard.press('z');
    const sent = await page.evaluate(() => (window as unknown as { __fakeMidiSent: number[][] }).__fakeMidiSent);
    expect(sent).toContainEqual([0x90, 60, 102]);
    expect(sent).toContainEqual([0x80, 60, 0]);

    const log = await page.evaluate(() =>
      (window as unknown as { __bespoke: { getMidiOutLog: () => Array<{ bytes: number[] }> } }).__bespoke.getMidiOutLog(),
    );
    expect(log.map((entry) => entry.bytes)).toContainEqual([0x90, 60, 102]);
  });

  for (const audio of AUDIO_BACKENDS) {
    test(`20 undo steps restore the graph while audio keeps playing [${audio}]`, async ({ page }) => {
      test.setTimeout(60000);
      await gotoApp(page, `audio=${audio}`);
      await waitForBespokeReady(page);
      await page.locator('#playBtn').click();

      const result = await page.evaluate(async () => {
        const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
        const canonical = () => {
          const state = JSON.parse(api.getStateJson() as string);
          // Output "level" is a live meter written by the audio path, not patch state.
          for (const m of state.modules) if (m.type === 'output') delete m.controls.level;
          const modules = [...state.modules].sort((a: { id: number }, b: { id: number }) => a.id - b.id);
          const connections = [...state.connections].map((c: Record<string, unknown>) => JSON.stringify(c)).sort();
          return JSON.stringify({ modules, connections });
        };
        // Yield to the event loop so audio callbacks interleave with the edits/undos.
        const frame = () => new Promise((resolve) => setTimeout(resolve, 0));
        const states = [canonical()];
        const ids: Record<string, number> = {};
        const edits: Array<() => void> = [
          () => (ids.a = api.createModule('oscillator', 60, 420) as number),
          () => (ids.b = api.createModule('delay', 260, 420) as number),
          () => (ids.c = api.createModule('reverb', 460, 420) as number),
          () => api.connectModules(ids.a, ids.b),
          () => api.connectModules(ids.b, ids.c),
          () => api.setControlValue(ids.b, 'feedback', 0.6),
          () => api.setControlValue(ids.a, 'frequency', 330),
          () => (ids.d = api.createModule('noise', 60, 560) as number),
          () => api.connectModules(ids.d, ids.c),
          () => api.setControlValue(ids.d, 'volume', 0.2),
          () => (ids.e = api.createModule('lfo', 660, 420) as number),
          () => api.setControlValue(ids.e, 'rate', 5),
          () => api.deleteModule(ids.b),
          () => api.connectModules(ids.a, ids.c),
          () => (ids.f = api.createModule('gain', 660, 560) as number),
          () => api.connectModules(ids.c, ids.f),
          () => api.setControlValue(ids.f, 'gain', 0.25),
          () => api.deleteModule(ids.d),
          () => api.setControlValue(ids.a, 'volume', 0.3),
          () => api.deleteModule(ids.c),
        ];
        for (const edit of edits) {
          edit();
          await frame();
          states.push(canonical());
        }
        const depth = api.getUndoDepth() as number;
        const callbacksBefore = (api.getAudioHealth() as { callbackCount: number }).callbackCount;
        let mismatches = 0;
        for (let i = edits.length; i > 0; i--) {
          if (!api.undo()) mismatches++;
          await frame();
          if (canonical() !== states[i - 1]) mismatches++;
        }
        await new Promise((resolve) => setTimeout(resolve, 200));
        const callbacksAfter = (api.getAudioHealth() as { callbackCount: number }).callbackCount;
        return { depth, mismatches, callbacksBefore, callbacksAfter, remaining: api.getUndoDepth() };
      });

      expect(result.depth).toBe(20);
      expect(result.mismatches).toBe(0);
      expect(result.remaining).toBe(0);
      expect(result.callbacksAfter).toBeGreaterThan(result.callbacksBefore);

      // Keyboard shortcut path: Ctrl+Z undoes a spawn, Ctrl+Shift+Z redoes it.
      const counts = await page.evaluate(() => {
        const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
        const before = api.getModuleCount() as number;
        api.createModule('noise', 40, 600);
        return { before, after: api.getModuleCount() as number };
      });
      await page.locator('#canvas').focus();
      await page.keyboard.press('Control+z');
      expect(await page.evaluate(() => (window as unknown as { __bespoke: { getModuleCount: () => number } }).__bespoke.getModuleCount())).toBe(counts.before);
      await page.keyboard.press('Control+Shift+z');
      expect(await page.evaluate(() => (window as unknown as { __bespoke: { getModuleCount: () => number } }).__bespoke.getModuleCount())).toBe(counts.after);
    });
  }

  test('Scala tuning: .scl drop retunes the Scale and persists in the patch', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);

    await page.evaluate((text) => {
      const canvas = document.getElementById('canvas')!;
      const transfer = new DataTransfer();
      transfer.items.add(new File([text], 'just_major.scl', { type: 'text/plain' }));
      canvas.dispatchEvent(new DragEvent('drop', { dataTransfer: transfer, bubbles: true, cancelable: true }));
    }, JUST_MAJOR_SCL);

    await expect
      .poll(() =>
        page.evaluate(() => {
          const api = (window as unknown as { __bespoke: { getNoteFrequency: (p: number) => number } }).__bespoke;
          return api.getNoteFrequency(62) / api.getNoteFrequency(60);
        }),
      )
      .toBeCloseTo(1.25, 4);

    const json = await page.evaluate(() =>
      (window as unknown as { __bespoke: { getStateJson: () => string } }).__bespoke.getStateJson(),
    );
    expect(json).toContain('"scl"');
    expect(json).toContain('just_major.scl');

    await page.reload();
    await waitForBespokeReady(page);
    const ratios = await page.evaluate((patch) => {
      const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
      const tet = (api.getNoteFrequency(62) as number) / (api.getNoteFrequency(60) as number);
      api.loadStateJson(patch);
      const loaded = (api.getNoteFrequency(62) as number) / (api.getNoteFrequency(60) as number);
      const bad = api.loadTuning('scl', 'not a scala file', 'bad.scl') as { ok: boolean };
      const afterBad = (api.getNoteFrequency(69) as number);
      return { tet, loaded, badOk: bad.ok, afterBad };
    }, json);
    expect(ratios.tet).toBeCloseTo(Math.pow(2, 2 / 12), 4);
    expect(ratios.loaded).toBeCloseTo(1.25, 4);
    expect(ratios.badOk).toBe(false);
    expect(ratios.afterBad).toBeCloseTo(440, 1);
  });

  test.describe('touch', () => {
    test.use({ hasTouch: true });

    test('one-finger drag moves a module; pinch zooms; long-press opens the spawn menu', async ({ page, browserName }) => {
      test.skip(browserName !== 'chromium', 'CDP touch injection is Chromium-only');
      await gotoApp(page);
      await waitForBespokeReady(page);

      const output = await page.evaluate(() => {
        const api = (window as unknown as { __bespoke: { getModules: () => Array<{ id: number; type: string }> } }).__bespoke;
        return api.getModules().find((m) => m.type === 'output')!.id;
      });
      const before = await moduleInfo(page, output);
      const box = await canvasBox(page);
      const cdp = await page.context().newCDPSession(page);
      const touch = (type: string, points: Array<{ x: number; y: number; id: number }>) =>
        cdp.send('Input.dispatchTouchEvent', {
          type,
          touchPoints: points.map((p) => ({ x: p.x, y: p.y, id: p.id, radiusX: 2, radiusY: 2, force: 1 })),
        });

      // Drag the output module by its title bar.
      const startX = box.x + Number(before.screenX) + 30;
      const startY = box.y + Number(before.screenY) + 8;
      await touch('touchStart', [{ x: startX, y: startY, id: 1 }]);
      for (let step = 1; step <= 6; step++) {
        await touch('touchMove', [{ x: startX + step * 20, y: startY + step * 10, id: 1 }]);
      }
      await touch('touchEnd', []);
      const after = await moduleInfo(page, output);
      expect(Number(after.x) - Number(before.x)).toBeGreaterThan(100);
      expect(Number(after.y) - Number(before.y)).toBeGreaterThan(40);

      // Undo covers touch moves too.
      await page.evaluate(() => (window as unknown as { __bespoke: { undo: () => boolean } }).__bespoke.undo());
      expect(Number((await moduleInfo(page, output)).x)).toBeCloseTo(Number(before.x), 3);

      // Two fingers spreading apart zoom the canvas in.
      const cx = box.x + box.width * 0.5;
      const cy = box.y + box.height * 0.75;
      await touch('touchStart', [{ x: cx - 40, y: cy, id: 2 }]);
      await touch('touchStart', [{ x: cx - 40, y: cy, id: 2 }, { x: cx + 40, y: cy, id: 3 }]);
      for (let step = 1; step <= 5; step++) {
        await touch('touchMove', [{ x: cx - 40 - step * 16, y: cy, id: 2 }, { x: cx + 40 + step * 16, y: cy, id: 3 }]);
      }
      await touch('touchEnd', []);
      expect(Number((await moduleInfo(page, output)).scale)).toBeGreaterThan(1.5);

      // Long-press on empty canvas opens the spawn menu.
      await touch('touchStart', [{ x: box.x + 30, y: box.y + box.height - 60, id: 4 }]);
      await page.waitForTimeout(750);
      await touch('touchEnd', []);
      expect(await page.evaluate(() => (window as unknown as { __bespoke: { isSpawnMenuOpen: () => boolean } }).__bespoke.isSpawnMenuOpen())).toBe(true);
    });
  });
});

test.describe('Script module (?script=1)', () => {
  for (const audio of AUDIO_BACKENDS) {
    test(`script sets oscillator frequency on beat 1 and cannot fetch [${audio}]`, async ({ page }) => {
      const leaked: string[] = [];
      page.on('request', (request) => {
        if (request.url().includes('should-not-happen')) leaked.push(request.url());
      });
      await gotoApp(page, `script=1&audio=${audio}`);
      await waitForBespokeReady(page);
      await expect(page.locator('#scriptBtn')).toBeVisible();
      await page.locator('#playBtn').click();

      const osc = await page.evaluate(() => {
        const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
        const id = (api.getModules() as Array<{ id: number; type: string }>).find((m) => m.type === 'oscillator')!.id;
        api.setControlValue(id, 'frequency', 100);
        api.runScript(`
          const osc = bespoke.findModule('oscillator');
          bespoke.log('fetch:' + typeof fetch, 'xhr:' + typeof XMLHttpRequest, 'ws:' + typeof WebSocket, 'wasm:' + typeof WebAssembly);
          try { fetch('/should-not-happen'); bespoke.log('fetch-called'); } catch (e) { bespoke.log('fetch-blocked'); }
          bespoke.onBeat((beat) => {
            if (beat % 4 === 0) {
              bespoke.setControl(osc, 'frequency', 330);
              bespoke.log('beat1:' + beat);
            }
          });
        `);
        return id;
      });

      await expect
        .poll(async () => page.evaluate(() => JSON.stringify((window as unknown as { __bespoke: { getScriptLog: () => unknown } }).__bespoke.getScriptLog())), { timeout: 8000 })
        .toContain('beat1:');
      const state = await page.evaluate((id) => {
        const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
        return {
          frequency: api.getControlValue(id, 'frequency') as number,
          status: api.getScriptStatus(),
          log: (api.getScriptLog() as Array<{ text: string }>).map((e) => e.text).join('\n'),
          undoDepth: api.getUndoDepth() as number,
        };
      }, osc);
      expect(state.frequency).toBe(330);
      expect(state.status).toBe('running');
      expect(state.log).toContain('fetch:undefined xhr:undefined ws:undefined wasm:undefined');
      expect(state.log).toContain('fetch-blocked');
      expect(state.log).not.toContain('fetch-called');
      expect(leaked).toEqual([]);

      // Dynamic import is refused before it reaches a worker.
      const importStatus = await page.evaluate(() => {
        const api = (window as unknown as { __bespoke: Record<string, (...a: unknown[]) => unknown> }).__bespoke;
        api.runScript('import("/should-not-happen.js")');
        return api.getScriptStatus();
      });
      expect(importStatus).toBe('error');
      expect(leaked).toEqual([]);
    });
  }

  test('a hung script is terminated and recreated', async ({ page }) => {
    test.setTimeout(30000);
    await gotoApp(page, 'script=1');
    await waitForBespokeReady(page);
    await page.evaluate(() => {
      (window as unknown as { __bespoke: { runScript: (code: string) => number } }).__bespoke.runScript('while (true) {}');
    });
    await expect
      .poll(() => page.evaluate(() => (window as unknown as { __bespoke: { getScriptStatus: () => string } }).__bespoke.getScriptStatus()), {
        timeout: 15000,
      })
      .toBe('killed');
    const log = await page.evaluate(() =>
      (window as unknown as { __bespoke: { getScriptLog: () => Array<{ text: string }> } }).__bespoke
        .getScriptLog()
        .map((e) => e.text)
        .join('\n'),
    );
    expect(log).toContain('restarted worker');
    // The UI thread stayed responsive throughout.
    expect(await page.evaluate(() => (window as unknown as { __bespoke: { getModuleCount: () => number } }).__bespoke.getModuleCount())).toBeGreaterThan(0);
  });
});
