import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

export interface RenderLoopHandle {
  stop: () => void;
  getFrameCount: () => number;
}

export interface RenderLoopOptions {
  module: BespokeSynthModule;
  canvas: HTMLCanvasElement;
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
