import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { withCStrings } from '../app/wasmStrings';
import { loadPatchStateJson, placeholderNotice } from '../patchState';
import { writeShareUrl } from '../samples/patchShare';
import { parseDemoIndex, type DemoEntry } from './demoIndex';

export type { DemoEntry } from './demoIndex';

const DEMO_DIR = '/resource/demos/';

function readResourceText(module: BespokeSynthModule, path: string): string {
  return withCStrings(module, [path], (ptr) => module.UTF8ToString(module._bespoke_read_resource_text(ptr)));
}

export function readDemoIndex(module: BespokeSynthModule): DemoEntry[] {
  return parseDemoIndex(readResourceText(module, `${DEMO_DIR}index.json`));
}

/** The demo's patch JSON ("" when the pack lacks it); file names come from the bundled index only. */
export function readDemoPatch(module: BespokeSynthModule, demo: DemoEntry): string {
  if (!/^[a-z0-9-]+\.json$/.test(demo.file)) return '';
  return readResourceText(module, DEMO_DIR + demo.file);
}

export interface DemoGalleryCallbacks {
  showPatchStatus: (message: string, durationMs?: number) => void;
}

/** Header "Demos" button: a popover listing the bundled demos; a click loads one, Share copies its link. */
export function setupDemoGallery(
  headerControls: Element,
  getModule: () => BespokeSynthModule | null,
  callbacks: DemoGalleryCallbacks,
): void {
  const wrap = document.createElement('div');
  wrap.className = 'midi-control demo-gallery';

  const button = document.createElement('button');
  button.id = 'demoGalleryBtn';
  button.className = 'btn';
  button.textContent = 'Demos';
  button.title = 'Example patches: load one with a click, or share its link';
  button.setAttribute('aria-haspopup', 'true');
  button.setAttribute('aria-expanded', 'false');

  const popover = document.createElement('div');
  popover.id = 'demoGallery';
  popover.className = 'midi-popover demo-gallery-popover';
  popover.hidden = true;
  popover.setAttribute('role', 'dialog');
  popover.setAttribute('aria-label', 'Demo gallery');

  const setOpen = (open: boolean): void => {
    popover.hidden = !open;
    button.setAttribute('aria-expanded', String(open));
  };

  const load = (module: BespokeSynthModule, demo: DemoEntry): void => {
    const json = readDemoPatch(module, demo);
    if (json && loadPatchStateJson(module, json)) {
      callbacks.showPatchStatus(`Loaded demo "${demo.title}" - Space plays${placeholderNotice(module)}`);
      setOpen(false);
    } else {
      callbacks.showPatchStatus(`Demo "${demo.title}" failed to load`);
    }
  };

  const share = async (module: BespokeSynthModule, demo: DemoEntry): Promise<void> => {
    const json = readDemoPatch(module, demo);
    if (!json) {
      callbacks.showPatchStatus(`Demo "${demo.title}" is missing`);
      return;
    }
    const url = await writeShareUrl(json);
    try {
      await navigator.clipboard.writeText(url);
      callbacks.showPatchStatus(`Share link for "${demo.title}" copied`);
    } catch {
      callbacks.showPatchStatus(`Share link for "${demo.title}" is in the address bar`);
    }
  };

  const render = (module: BespokeSynthModule): void => {
    const demos = readDemoIndex(module);
    const list = document.createElement('ul');
    list.className = 'demo-gallery-list';
    for (const demo of demos) {
      const item = document.createElement('li');
      item.className = 'demo-gallery-item';
      item.dataset.demo = demo.id;

      const open = document.createElement('button');
      open.type = 'button';
      open.className = 'demo-gallery-open';
      open.title = `Load "${demo.title}"`;
      const title = document.createElement('span');
      title.className = 'demo-gallery-title';
      title.textContent = demo.title;
      const description = document.createElement('span');
      description.className = 'demo-gallery-description';
      description.textContent = demo.description;
      const modules = document.createElement('span');
      modules.className = 'demo-gallery-modules';
      modules.textContent = demo.modules.join(' · ');
      open.append(title, description, modules);
      open.addEventListener('click', () => load(module, demo));

      const shareButton = document.createElement('button');
      shareButton.type = 'button';
      shareButton.className = 'btn demo-gallery-share';
      shareButton.textContent = 'Share';
      shareButton.title = `Copy a link that opens "${demo.title}"`;
      shareButton.addEventListener('click', () => void share(module, demo));

      item.append(open, shareButton);
      list.appendChild(item);
    }
    if (demos.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'midi-popover-hint';
      empty.textContent = 'No demos in this build.';
      popover.replaceChildren(empty);
      return;
    }
    popover.replaceChildren(list);
  };

  button.addEventListener('click', (event) => {
    event.stopPropagation();
    if (!popover.hidden) {
      setOpen(false);
      return;
    }
    const module = getModule();
    if (!module) return;
    render(module);
    setOpen(true);
  });
  document.addEventListener('click', (event) => {
    if (!wrap.contains(event.target as Node)) setOpen(false);
  });
  popover.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      setOpen(false);
      button.focus();
    }
  });

  wrap.append(button, popover);
  headerControls.appendChild(wrap);
}
