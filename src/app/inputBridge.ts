import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

export interface InputBridgeCallbacks {
  onPlay?: () => void;
  onStop?: () => void;
}

let inputBridgeInstalled = false;

function getModifiers(e: KeyboardEvent): number {
  let modifiers = 0;
  if (e.shiftKey) modifiers |= 1;
  if (e.altKey) modifiers |= 2;
  if (e.ctrlKey) modifiers |= 4;
  if (e.metaKey) modifiers |= 8;
  return modifiers;
}

/**
 * Single install point for DOM input → WASM bridge calls.
 * Idempotent: subsequent calls are no-ops.
 */
export function installInputBridge(
  canvas: HTMLCanvasElement,
  module: BespokeSynthModule,
  callbacks: InputBridgeCallbacks = {},
): void {
  if (inputBridgeInstalled) return;
  inputBridgeInstalled = true;

  canvas.addEventListener('mousedown', (e) => {
    module._bespoke_mouse_down?.(e.offsetX, e.offsetY, e.button);
  });

  canvas.addEventListener('contextmenu', (e) => e.preventDefault());

  canvas.addEventListener('mouseup', (e) => {
    module._bespoke_mouse_up?.(e.offsetX, e.offsetY, e.button);
  });

  canvas.addEventListener('mousemove', (e) => {
    module._bespoke_mouse_move?.(e.offsetX, e.offsetY);
  });

  canvas.addEventListener(
    'wheel',
    (e) => {
      e.preventDefault();
      module._bespoke_mouse_wheel?.(e.deltaX, e.deltaY);
    },
    { passive: false },
  );

  document.addEventListener('keydown', (e) => {
    if (!module._bespoke_key_down) return;
    const modifiers = getModifiers(e);
    const keyCode = e.key.length === 1 ? e.key.toUpperCase().charCodeAt(0) : (e.keyCode || e.which);
    if (e.key === '/' || (e.ctrlKey && e.key.toLowerCase() === 'k')) e.preventDefault();
    module._bespoke_key_down(keyCode, modifiers);
  });

  document.addEventListener('keyup', (e) => {
    if (!module._bespoke_key_up) return;
    const modifiers = getModifiers(e);
    const keyCode = e.code ? e.code.charCodeAt(0) : (e.keyCode || e.which);
    module._bespoke_key_up(keyCode, modifiers);
  });

  const playBtn = document.getElementById('playBtn');
  const stopBtn = document.getElementById('stopBtn');

  playBtn?.addEventListener('click', () => {
    callbacks.onPlay?.();
  });

  stopBtn?.addEventListener('click', () => {
    callbacks.onStop?.();
  });
}

/** Test-only: reset idempotency guard between page loads in unit tests. */
export function resetInputBridgeForTests(): void {
  inputBridgeInstalled = false;
}
