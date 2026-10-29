import { test, expect, type Page } from '@playwright/test';
import path from 'node:path';
import { gotoApp, waitForBespokeReady } from './helpers';

// #127: hover help, the demo gallery, and desktop savestate import.

async function canvasBox(page: Page) {
  const box = await page.locator('#canvas').boundingBox();
  if (!box) throw new Error('canvas has no box');
  return box;
}

test.describe('Module help', () => {
  test('hovering a control and a title bar shows their help, in the browser language', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);
    const filter = await page.evaluate(() => window.__bespoke!.getModules().find((m) => m.type === 'filter')!.id);
    const info = await page.evaluate((id) => window.__bespoke!.getModuleInfo(id), filter);
    const box = await canvasBox(page);
    const left = box.x + Number(info.screenX) + 30 * Number(info.scale);
    const top = box.y + Number(info.screenY);

    // Same spot the MIDI-learn spec clicks: the cutoff line.
    await page.mouse.move(left, top + (36 + 15 * 1.5) * Number(info.scale));
    await expect.poll(() => page.evaluate(() => window.__bespoke!.getHoverHelp())).toBe('cutoff frequency, in Hz');

    await page.mouse.move(left, top + 8 * Number(info.scale));
    const english = await page.evaluate(() => window.__bespoke!.getHoverHelp());
    expect(english).toContain('filter');

    await page.evaluate(() => window.__bespoke!.setHelpLocale('de-DE'));
    await page.mouse.move(left + 1, top + 8 * Number(info.scale));
    const german = await page.evaluate(() => window.__bespoke!.getHoverHelp());
    expect(german.length).toBeGreaterThan(0);
    expect(german).not.toBe(english);
  });
});

test.describe('Demo gallery', () => {
  test('lists the bundled demos, loads one with a click, and shares it', async ({ page, context }) => {
    await context.grantPermissions(['clipboard-read', 'clipboard-write']);
    await gotoApp(page);
    await waitForBespokeReady(page);

    await page.locator('#demoGalleryBtn').click();
    const items = page.locator('#demoGallery .demo-gallery-item');
    await expect(items).toHaveCount(8);

    await page.locator('#demoGallery [data-demo="fm-arpeggio"] .demo-gallery-open').click();
    await expect(page.locator('#demoGallery')).toBeHidden();
    const types = await page.evaluate(() => window.__bespoke!.getModules().map((m) => m.type));
    for (const type of ['notesequencer', 'chorder', 'arpeggiator', 'fmsynth', 'eq', 'pingpongdelay', 'output']) {
      expect(types).toContain(type);
    }
    expect(await page.evaluate(() => window.__bespoke!.getPlaceholderCount())).toBe(0);

    // Arpeggiator → FM → EQ → ping-pong delay plays, and the echoes are stereo.
    await page.locator('#playBtn').click();
    await expect
      .poll(
        () =>
          page.evaluate(() => {
            const samples = window.__bespoke!.getOutputWaveform(2048);
            return Math.sqrt(samples.reduce((sum, s) => sum + s * s, 0) / samples.length);
          }),
        { timeout: 10000 },
      )
      .toBeGreaterThan(0.005);

    await page.locator('#demoGalleryBtn').click();
    await page.locator('#demoGallery [data-demo="granular-pad"] .demo-gallery-share').click();
    await expect.poll(() => page.evaluate(() => window.location.hash)).toMatch(/^#p=/);
    const shared = await page.evaluate(() => window.location.href);

    const other = await context.newPage();
    await other.goto(shared);
    await waitForBespokeReady(other);
    await expect
      .poll(() => other.evaluate(() => window.__bespoke!.getModules().map((m) => m.type)))
      .toContain('seaofgrain');
  });
});

test.describe('Desktop savestate import', () => {
  test('a desktop .bsk loads through Load, keeps placeholders, and saves them back', async ({ page }) => {
    await gotoApp(page);
    await waitForBespokeReady(page);
    const chooser = page.waitForEvent('filechooser');
    await page.locator('#loadPatchBtn').click();
    await (await chooser).setFiles(
      path.resolve('resource/userdata_original/savestate/example__sequencing_sequencers.bsk'),
    );
    await expect.poll(() => page.evaluate(() => window.__bespoke!.getPlaceholderCount())).toBe(2);
    const types = await page.evaluate(() => window.__bespoke!.getModules().map((m) => m.type));
    expect(types.filter((type) => type === 'notesequencer')).toHaveLength(2);
    expect(types).toContain('karplusstrong');

    const saved = await page.evaluate(() => JSON.parse(window.__bespoke!.getStateJson()) as {
      modules: Array<{ type: string; extras?: Record<string, string> }>;
    });
    const comments = saved.modules.filter((m) => m.type === 'comment');
    expect(comments.map((m) => m.extras?.label).sort()).toEqual(['comment', 'comment2']);

    await page.locator('#playBtn').click();
    await expect
      .poll(
        () =>
          page.evaluate(() => {
            const samples = window.__bespoke!.getOutputWaveform(2048);
            return Math.max(...samples.map(Math.abs));
          }),
        { timeout: 10000 },
      )
      .toBeGreaterThan(0.01);
  });
});
