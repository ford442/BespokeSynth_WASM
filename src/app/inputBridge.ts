import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { isEditableTarget, keyboardEventToWasmKeyCode } from './keyMapping';
import type { QwertyPiano } from './qwertyPiano';

export interface InputBridgeCallbacks {
  onPlay?: () => void;
  onStop?: () => void;
  /** Computer-keyboard piano; gets first look at unmodified keys. */
  piano?: QwertyPiano;
}

let inputBridgeInstalled = false;
let lastKeyDownCode = -1;
let lastKeyUpCode = -1;

/** Touch hold without movement → spawn menu (right-click equivalent). */
export const LONG_PRESS_MS = 500;
const LONG_PRESS_SLOP_PX = 10;

function getModifiers(e: KeyboardEvent | MouseEvent): number {
  let modifiers = 0;
  if (e.shiftKey) modifiers |= 1;
  if (e.altKey) modifiers |= 2;
  if (e.ctrlKey) modifiers |= 4;
  if (e.metaKey) modifiers |= 8;
  return modifiers;
}

export function getLastKeyCodes(): { down: number; up: number } {
  return { down: lastKeyDownCode, up: lastKeyUpCode };
}

interface TouchPoint {
  x: number;
  y: number;
  startX: number;
  startY: number;
}

/**
 * Pointer Events (mouse, touch, pen) → WASM. The canvas runs one gesture at a
 * time keyed by pointerId; two touches become pinch-zoom + pan, and a still
 * touch held for LONG_PRESS_MS opens the spawn menu.
 */
function installPointerInput(canvas: HTMLCanvasElement, module: BespokeSynthModule): void {
  const touches = new Map<number, TouchPoint>();
  let gesturePointer = -1;
  let pinching = false;
  let pinchDistance = 0;
  let pinchMidX = 0;
  let pinchMidY = 0;
  let longPressTimer: number | null = null;

  const clearLongPress = () => {
    if (longPressTimer !== null) window.clearTimeout(longPressTimer);
    longPressTimer = null;
  };

  const pinchMetrics = () => {
    const [a, b] = [...touches.values()];
    return {
      distance: Math.hypot(b.x - a.x, b.y - a.y),
      midX: (a.x + b.x) / 2,
      midY: (a.y + b.y) / 2,
    };
  };

  const cancelGesture = () => {
    if (gesturePointer >= 0) module._bespoke_pointer_cancel(gesturePointer);
    gesturePointer = -1;
  };

  canvas.addEventListener('pointerdown', (e) => {
    try {
      canvas.setPointerCapture(e.pointerId);
    } catch {
      // Synthetic events have no capturable pointer.
    }

    if (e.pointerType === 'touch') {
      e.preventDefault();
      touches.set(e.pointerId, { x: e.offsetX, y: e.offsetY, startX: e.offsetX, startY: e.offsetY });
      if (touches.size === 2) {
        // Second finger: abandon the one-finger drag and zoom/pan instead.
        clearLongPress();
        cancelGesture();
        pinching = true;
        const metrics = pinchMetrics();
        pinchDistance = metrics.distance;
        pinchMidX = metrics.midX;
        pinchMidY = metrics.midY;
        return;
      }
      if (touches.size > 2 || pinching) return;
      const x = e.offsetX;
      const y = e.offsetY;
      const pointerId = e.pointerId;
      clearLongPress();
      longPressTimer = window.setTimeout(() => {
        longPressTimer = null;
        if (gesturePointer !== pointerId) return;
        cancelGesture();
        module._bespoke_long_press(x, y);
      }, LONG_PRESS_MS);
    }

    gesturePointer = e.pointerId;
    module._bespoke_pointer_down(e.pointerId, e.offsetX, e.offsetY, e.button, getModifiers(e));
  });

  canvas.addEventListener('pointermove', (e) => {
    if (e.pointerType === 'touch') {
      const point = touches.get(e.pointerId);
      if (!point) return;
      point.x = e.offsetX;
      point.y = e.offsetY;
      if (Math.hypot(point.x - point.startX, point.y - point.startY) > LONG_PRESS_SLOP_PX) clearLongPress();
      if (pinching) {
        if (touches.size < 2) return;
        const metrics = pinchMetrics();
        if (pinchDistance > 0 && metrics.distance > 0) {
          module._bespoke_canvas_zoom(metrics.distance / pinchDistance, metrics.midX, metrics.midY);
        }
        module._bespoke_canvas_pan(metrics.midX - pinchMidX, metrics.midY - pinchMidY);
        pinchDistance = metrics.distance;
        pinchMidX = metrics.midX;
        pinchMidY = metrics.midY;
        return;
      }
    }
    module._bespoke_pointer_move(e.pointerId, e.offsetX, e.offsetY);
  });

  const endPointer = (e: PointerEvent, cancelled: boolean) => {
    if (e.pointerType === 'touch') {
      touches.delete(e.pointerId);
      clearLongPress();
      if (pinching) {
        // Stay in pinch mode until every finger lifts; no stray drag afterwards.
        if (touches.size === 0) pinching = false;
        return;
      }
    }
    if (gesturePointer !== e.pointerId) return;
    gesturePointer = -1;
    if (cancelled) module._bespoke_pointer_cancel(e.pointerId);
    else module._bespoke_pointer_up(e.pointerId, e.offsetX, e.offsetY, e.button);
  };

  canvas.addEventListener('pointerup', (e) => endPointer(e, false));
  canvas.addEventListener('pointercancel', (e) => endPointer(e, true));
  canvas.addEventListener('lostpointercapture', (e) => {
    if (gesturePointer === e.pointerId) endPointer(e, true);
  });
}

function installMouseInput(canvas: HTMLCanvasElement, module: BespokeSynthModule): void {
  canvas.addEventListener('mousedown', (e) => {
    module._bespoke_mouse_down?.(e.offsetX, e.offsetY, e.button);
  });
  canvas.addEventListener('mouseup', (e) => {
    module._bespoke_mouse_up?.(e.offsetX, e.offsetY, e.button);
  });
  canvas.addEventListener('mousemove', (e) => {
    module._bespoke_mouse_move?.(e.offsetX, e.offsetY);
  });
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

  if (typeof window.PointerEvent === 'function' && typeof module._bespoke_pointer_down === 'function') {
    installPointerInput(canvas, module);
  } else {
    installMouseInput(canvas, module);
  }

  canvas.addEventListener('contextmenu', (e) => e.preventDefault());

  canvas.addEventListener(
    'wheel',
    (e) => {
      e.preventDefault();
      module._bespoke_mouse_wheel?.(e.deltaX, e.deltaY);
    },
    { passive: false },
  );

  const piano = callbacks.piano;

  document.addEventListener('keydown', (e) => {
    if (!module._bespoke_key_down || isEditableTarget(e.target)) return;
    const modifiers = getModifiers(e);
    const keyCode = keyboardEventToWasmKeyCode(e);
    lastKeyDownCode = keyCode;

    // The spawn-menu search box owns letters while it is open.
    const typingInCanvas = (module._bespoke_is_text_entry_active?.() ?? 0) === 1;
    if (piano && !typingInCanvas && piano.handleKeyDown(e)) {
      e.preventDefault();
      return;
    }

    const lower = e.key.toLowerCase();
    if (e.key === '/' || (e.ctrlKey && lower === 'k')) e.preventDefault();
    // Undo/redo are canvas commands, not browser history.
    if ((e.ctrlKey || e.metaKey) && (e.code === 'KeyZ' || e.code === 'KeyY')) e.preventDefault();
    module._bespoke_key_down(keyCode, modifiers);
  });

  document.addEventListener('keyup', (e) => {
    if (!module._bespoke_key_up) return;
    const modifiers = getModifiers(e);
    const keyCode = keyboardEventToWasmKeyCode(e);
    lastKeyUpCode = keyCode;
    // Note-offs must land even if focus moved to a text field mid-note.
    if (piano?.handleKeyUp(e)) return;
    if (isEditableTarget(e.target)) return;
    module._bespoke_key_up(keyCode, modifiers);
  });

  window.addEventListener('blur', () => piano?.releaseAll());

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
  lastKeyDownCode = -1;
  lastKeyUpCode = -1;
}
