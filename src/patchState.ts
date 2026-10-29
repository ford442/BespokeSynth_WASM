import type { BespokeSynthModule } from '../wasm/types/bespoke-synth';
import { MAX_PATCH_BYTES, validatePatchJson } from './patchStorage';
import { wasmMalloc } from './app/wasmStrings';
import { isDesktopSavestate } from './samples/desktopSavestate';

export function getPatchStateJson(module: BespokeSynthModule | null): string {
  if (!module) return '{}';
  const ptr = module._bespoke_get_state_json();
  if (!ptr) return '{}';
  return module.UTF8ToString(ptr);
}

export function loadPatchStateJson(module: BespokeSynthModule | null, json: string): boolean {
  if (!module) return false;
  const jsonPtr = module.stringToNewUTF8(json);
  try {
    return module._bespoke_load_state_json(jsonPtr) === 0;
  } finally {
    module._free(jsonPtr);
  }
}

/** Modules in the current patch whose type this build lacks (kept as placeholders, saved back unchanged). */
export function getPlaceholderCount(module: BespokeSynthModule | null): number {
  return module?._bespoke_get_placeholder_count() ?? 0;
}

/** Suffix for a "Loaded ..." status line when the patch held module types this build doesn't have. */
export function placeholderNotice(module: BespokeSynthModule | null): string {
  const count = getPlaceholderCount(module);
  if (count <= 0) return '';
  return ` - ${count} unsupported module${count === 1 ? '' : 's'} kept as placeholder${count === 1 ? '' : 's'}`;
}

/** Load a patch bundled in Emscripten's preloaded /resource filesystem. */
export function loadBundledLayout(module: BespokeSynthModule | null, path: string): boolean {
  if (!module) return false;
  const pathPtr = module.stringToNewUTF8(path);
  try {
    return module._bespoke_load_layout(pathPtr) === 0;
  } finally {
    module._free(pathPtr);
  }
}

export function downloadPatchState(module: BespokeSynthModule | null, filename = 'bespokesynth-patch.bsk'): void {
  const json = getPatchStateJson(module);
  const blob = new Blob([json], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
}

/** Desktop savestates carry each module's binary state after the layout; only the layout is read. */
const MAX_DESKTOP_SAVESTATE_BYTES = 64 * 1024 * 1024;

/** Load a desktop .bsk: its layout, with placeholders for module types this build lacks. */
export function importDesktopSavestate(module: BespokeSynthModule | null, bytes: Uint8Array): boolean {
  if (!module) return false;
  const ptr = wasmMalloc(module, bytes.length);
  try {
    module.HEAPU8.set(bytes, ptr);
    return module._bespoke_import_desktop_savestate(ptr, bytes.length) === 0;
  } finally {
    module._free(ptr);
  }
}

export async function loadPatchStateFile(module: BespokeSynthModule | null, file: File): Promise<boolean> {
  const head = new Uint8Array(await file.slice(0, 8).arrayBuffer());
  if (isDesktopSavestate(head)) {
    if (file.size > MAX_DESKTOP_SAVESTATE_BYTES) throw new Error('Desktop savestate exceeds the 64 MB limit');
    return importDesktopSavestate(module, new Uint8Array(await file.arrayBuffer()));
  }
  if (file.size > MAX_PATCH_BYTES) throw new Error('Patch exceeds the 5 MB limit');
  const json = await file.text();
  validatePatchJson(json);
  return loadPatchStateJson(module, json);
}

export function promptForPatchStateFile(module: BespokeSynthModule | null): Promise<boolean> {
  return new Promise((resolve) => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.bsk,.bskt,.json,application/json';
    input.addEventListener('change', () => {
      const file = input.files?.[0];
      if (!file) {
        resolve(false);
        return;
      }
      loadPatchStateFile(module, file).then(resolve).catch((error) => {
        console.error('Patch load failed:', error);
        resolve(false);
      });
    }, { once: true });
    // Dismissing the picker fires 'cancel', not 'change'.
    input.addEventListener('cancel', () => resolve(false), { once: true });
    input.click();
  });
}
