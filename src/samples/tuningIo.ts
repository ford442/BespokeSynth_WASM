import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { readCString, withCStrings } from '../app/wasmStrings';

export type TuningKind = 'scl' | 'kbm';

/** Scala files are tiny text files; anything larger is not one. */
const MAX_TUNING_BYTES = 256 * 1024;

export function tuningKindForFile(name: string): TuningKind | null {
  const lower = name.toLowerCase();
  if (lower.endsWith('.scl')) return 'scl';
  if (lower.endsWith('.kbm')) return 'kbm';
  return null;
}

export interface TuningLoadResult {
  ok: boolean;
  error: string;
}

/**
 * Hands Scala text to the Scale module, which parses it with libs/tuning-library
 * and republishes the note → Hz table. The text itself is saved in the patch
 * (Scale extras scl/kbm), so shared URLs carry the tuning. Empty text clears it.
 */
export function loadTuningText(module: BespokeSynthModule, kind: TuningKind, text: string, name = ''): TuningLoadResult {
  const ok = withCStrings(module, [kind, text, name], (kindPtr, textPtr, namePtr) =>
    module._bespoke_scale_set_tuning(kindPtr, textPtr, namePtr) === 1,
  );
  return { ok, error: ok ? '' : readCString(module, module._bespoke_scale_get_tuning_error()) };
}

export async function importTuningFile(module: BespokeSynthModule, file: File): Promise<TuningLoadResult> {
  const kind = tuningKindForFile(file.name);
  if (!kind) return { ok: false, error: 'Not a .scl or .kbm file' };
  if (file.size > MAX_TUNING_BYTES) return { ok: false, error: 'Tuning file is too large' };
  return loadTuningText(module, kind, await file.text(), file.name);
}
