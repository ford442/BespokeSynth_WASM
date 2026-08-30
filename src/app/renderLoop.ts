import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

export interface RenderLoopHandle {
  stop: () => void;
  getFrameCount: () => number;
}

export interface RenderLoopOptions {
  module: BespokeSynthModule;
  canvas: HTMLCanvasElement;
  syncOverlay?: (module: BespokeSynthModule, canvas: HTMLCanvasElement) => void;
}

/**
 * Owns the sole requestAnimationFrame driver for bespoke_render().
 */
export function startRenderLoop(options: RenderLoopOptions): RenderLoopHandle {
  let animationFrameId: number | null = null;
  let lastFrameTime = performance.now();
  let hostFrameCount = 0;

  const renderFrame = (timestamp: number) => {
    const deltaSeconds = Math.min(0.1, Math.max(0, (timestamp - lastFrameTime) / 1000));
    lastFrameTime = timestamp;

    options.module._bespoke_set_frame_delta?.(deltaSeconds);
    options.module._bespoke_render?.();
    hostFrameCount += 1;
    options.syncOverlay?.(options.module, options.canvas);

    animationFrameId = requestAnimationFrame(renderFrame);
  };

  animationFrameId = requestAnimationFrame(renderFrame);

  return {
    stop: () => {
      if (animationFrameId !== null) {
        cancelAnimationFrame(animationFrameId);
        animationFrameId = null;
      }
    },
    getFrameCount: () => hostFrameCount,
  };
}

export function syncControlOverlays(
  module: BespokeSynthModule,
  guiOverlay: HTMLElement | null,
  labelElements: Map<number, HTMLElement>,
): void {
  if (!guiOverlay) return;

  const count: number = module._bespoke_get_control_count?.() ?? 0;

  labelElements.forEach((el, idx) => {
    if (idx >= count) el.style.display = 'none';
  });

  if (count === 0) return;

  for (let i = 0; i < count; i++) {
    const ptr: number = module._bespoke_get_control_info?.(i) ?? 0;
    if (!ptr || !module.UTF8ToString) continue;

    let info: {
      label: string;
      value: number;
      unit: string;
      x: number;
      y: number;
      size: number;
    };
    try {
      info = JSON.parse(module.UTF8ToString(ptr));
    } catch {
      continue;
    }

    // info.x/info.y/info.size are authored in logical (CSS) pixels — the same
    // space passed to bespoke_resize() — so no scaling is needed to place the
    // DOM overlay, even though canvas.width/height are physical (HiDPI) pixels.
    const cssCenterX = info.x + info.size * 0.5;
    const cssBelowY = info.y + info.size * 1.15;

    let displayVal = '';
    if (info.unit === 'Hz') {
      const v = info.value;
      displayVal = v >= 1000 ? `${(v / 1000).toFixed(2)} kHz` : `${Math.round(v)} Hz`;
    } else if (info.unit === '%') {
      displayVal = `${Math.round(info.value * 100)}%`;
    } else {
      displayVal = info.value.toFixed(2) + (info.unit ? ` ${info.unit}` : '');
    }

    let el = labelElements.get(i);
    if (!el) {
      el = document.createElement('div');
      el.className = 'gui-label';
      el.innerHTML =
        '<span class="label-name"></span>' +
        '<span class="label-value"></span>';
      guiOverlay.appendChild(el);
      labelElements.set(i, el);
    }

    el.style.display = '';
    el.style.left = `${cssCenterX}px`;
    el.style.top = `${cssBelowY}px`;

    const nameEl = el.querySelector<HTMLElement>('.label-name');
    const valueEl = el.querySelector<HTMLElement>('.label-value');
    if (nameEl) nameEl.textContent = info.label;
    if (valueEl) valueEl.textContent = displayVal;
  }
}
