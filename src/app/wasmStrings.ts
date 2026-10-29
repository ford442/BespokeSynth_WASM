import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

/**
 * `_malloc` that throws instead of handing out 0: with ALLOW_MEMORY_GROWTH, malloc returns 0 once
 * the heap cannot grow (MAXIMUM_MEMORY), and writing through 0 corrupts low memory silently.
 */
export function wasmMalloc(module: BespokeSynthModule, bytes: number): number {
  const ptr = module._malloc(bytes);
  if (!ptr) throw new Error(`WASM heap exhausted (could not allocate ${bytes} bytes)`);
  return ptr;
}

/** Calls `fn` with heap copies of `values` as C strings, freeing them afterwards. */
export function withCStrings<T>(module: BespokeSynthModule, values: string[], fn: (...ptrs: number[]) => T): T {
  const ptrs = values.map((value) => module.stringToNewUTF8(value));
  try {
    return fn(...ptrs);
  } finally {
    ptrs.forEach((ptr) => module._free(ptr));
  }
}

export function readCString(module: BespokeSynthModule, ptr: number): string {
  return ptr ? module.UTF8ToString(ptr) : '';
}

export function readJson<T>(module: BespokeSynthModule, ptr: number, fallback: T): T {
  try {
    return ptr ? (JSON.parse(module.UTF8ToString(ptr)) as T) : fallback;
  } catch {
    return fallback;
  }
}
