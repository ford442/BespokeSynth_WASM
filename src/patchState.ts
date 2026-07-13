import type { BespokeSynthModule } from '../wasm/types/bespoke-synth';
import { MAX_PATCH_BYTES, validatePatchJson } from './patchStorage';

export function getPatchStateJson(module: BespokeSynthModule | null): string {
  if (!module) return '{}';
  const ptr = module._bespoke_get_state_json?.() ?? 0;
  if (!ptr) return '{}';
  return module.UTF8ToString(ptr);
}

export function loadPatchStateJson(module: BespokeSynthModule | null, json: string): boolean {
  if (!module?._bespoke_load_state_json || !module.allocateUTF8 || !module._free) {
    return false;
  }
  const jsonPtr = module.allocateUTF8(json);
  try {
    return module._bespoke_load_state_json(jsonPtr) === 0;
  } finally {
    module._free(jsonPtr);
  }
}

/** Load a patch bundled in Emscripten's preloaded /resource filesystem. */
export function loadBundledLayout(module: BespokeSynthModule | null, path: string): boolean {
  if (!module?._bespoke_load_layout || !module.allocateUTF8 || !module._free) return false;
  const pathPtr = module.allocateUTF8(path);
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

export async function loadPatchStateFile(module: BespokeSynthModule | null, file: File): Promise<boolean> {
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
    input.click();
  });
}
