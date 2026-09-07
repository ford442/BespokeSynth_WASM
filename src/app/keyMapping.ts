/** Map a KeyboardEvent to the classic key codes expected by WasmInputRouter. */
export function keyboardEventToWasmKeyCode(e: KeyboardEvent): number {
  if (e.key === 'Tab') return 9;
  if (e.key === ' ') return 32;
  if (e.key === 'Shift') return 16;
  if (e.key === 'Control') return 17;
  if (e.key === 'Alt') return 18;
  if (e.key === 'Meta') return 91;
  if (e.key === 'Escape') return 27;
  if (e.key === 'Enter') return 13;
  if (e.key === 'Backspace') return 8;
  if (e.key === 'ArrowLeft') return 37;
  if (e.key === 'ArrowUp') return 38;
  if (e.key === 'ArrowRight') return 39;
  if (e.key === 'ArrowDown') return 40;
  if (e.key.length === 1) return e.key.toUpperCase().charCodeAt(0);
  return e.keyCode || e.which || 0;
}
