// Desktop .bsk detection; no imports, so tests/unit can load it under node.

/**
 * True when `head` starts like a desktop .bsk: a little-endian FileStream string length
 * (high bytes zero) rather than JSON text. Mirrors looksLikeDesktopSavestate in
 * wasm/src/DesktopSavestate.cpp, which does the actual import.
 */
export function isDesktopSavestate(head: Uint8Array): boolean {
  return head.length >= 8 && head[0] !== 0x7b && head[4] === 0 && head[5] === 0 && head[6] === 0 && head[7] === 0;
}
