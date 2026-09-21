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

/** True when the event target takes text (the canvas must not see those keys). */
export function isEditableTarget(target: EventTarget | null): boolean {
  if (!target || typeof (target as Element).tagName !== 'string') return false;
  const element = target as HTMLElement;
  const tag = element.tagName.toUpperCase();
  return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || element.isContentEditable === true;
}
