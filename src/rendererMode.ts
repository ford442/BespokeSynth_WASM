import type { BespokeSynthModule } from '../wasm/types/bespoke-synth';

export type RendererBackend = 'webgpu' | 'webgl';

export interface ScreenshotRect {
  x?: number;
  y?: number;
  width?: number;
  height?: number;
}

export interface CaptureScreenshotOptions extends ScreenshotRect {
  /** Trigger bespoke_render() before capture (default true). */
  renderFirst?: boolean;
}

export type ScreenshotCallback = (dataUrl: string, info: {
  width: number;
  height: number;
  backend: RendererBackend;
}) => void;

const STORAGE_KEY = 'bespokesynth.renderer';

let screenshotCallback: ScreenshotCallback | null = null;

export function getStoredRendererPreference(): RendererBackend | null {
  try {
    const value = window.localStorage.getItem(STORAGE_KEY);
    return value === 'webgl' || value === 'webgpu' ? value : null;
  } catch {
    return null;
  }
}

export function setStoredRendererPreference(backend: RendererBackend): void {
  try {
    window.localStorage.setItem(STORAGE_KEY, backend);
  } catch {
    // Storage may be disabled in hardened test browsers.
  }
}

export function resolveRendererBackend(search: string = window.location.search): RendererBackend {
  const params = new URLSearchParams(search);
  const explicit = params.get('renderer')?.toLowerCase();
  if (explicit === 'webgl' || explicit === 'webgl2' || params.has('webgl')) return 'webgl';
  if (explicit === 'webgpu' || params.has('webgpu')) return 'webgpu';
  return getStoredRendererPreference() ?? 'webgpu';
}

export function isRenderTestRequested(search: string = window.location.search): boolean {
  const params = new URLSearchParams(search);
  return params.get('renderTest') === '1' || params.has('renderTest');
}

/**
 * Render-test scene id: 0 when absent, 2 for `?renderTest=2` (drum bus), otherwise 1
 * (canonical synth voice — also for a bare `?renderTest`).
 */
export function getRenderTestScene(search: string = window.location.search): number {
  const params = new URLSearchParams(search);
  if (!params.has('renderTest')) return 0;
  return params.get('renderTest') === '2' ? 2 : 1;
}

/** `?debugFont=pixel` keeps the 5x7 bitmap font instead of the SDF atlas. */
export function isPixelFontRequested(search: string = window.location.search): boolean {
  return new URLSearchParams(search).get('debugFont')?.toLowerCase() === 'pixel';
}

export function isLegacyPanelsRequested(search: string = window.location.search): boolean {
  const params = new URLSearchParams(search);
  return params.get('legacyPanels') === '1' || params.has('legacyPanels');
}

export function publishRendererBreadcrumbs(
  backend: RendererBackend,
  fallbackReason: string | null = null,
): void {
  const target = window as Window & {
    rendererType?: RendererBackend;
    usingWebGPU?: boolean;
    usingWebGL?: boolean;
    rendererFallbackReason?: string | null;
  };
  target.rendererType = backend;
  target.usingWebGPU = backend === 'webgpu';
  target.usingWebGL = backend === 'webgl';
  target.rendererFallbackReason = fallbackReason;
}

export function switchRendererPreference(backend: RendererBackend): void {
  setStoredRendererPreference(backend);
  const url = new URL(window.location.href);
  url.searchParams.set('renderer', backend);
  window.location.assign(url.toString());
}

export function onScreenshotCaptured(callback: ScreenshotCallback | null): void {
  screenshotCallback = callback;
}

function cropDataUrl(
  dataUrl: string,
  rect: ScreenshotRect,
  fullWidth: number,
  fullHeight: number,
): Promise<string> {
  const x = rect.x ?? 0;
  const y = rect.y ?? 0;
  const width = rect.width ?? fullWidth - x;
  const height = rect.height ?? fullHeight - y;

  if (x === 0 && y === 0 && width === fullWidth && height === fullHeight) {
    return Promise.resolve(dataUrl);
  }

  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      const canvas = document.createElement('canvas');
      canvas.width = width;
      canvas.height = height;
      const ctx = canvas.getContext('2d');
      if (!ctx) {
        reject(new Error('2D canvas context unavailable for crop'));
        return;
      }
      ctx.drawImage(img, x, y, width, height, 0, 0, width, height);
      resolve(canvas.toDataURL('image/png'));
    };
    img.onerror = () => reject(new Error('Failed to decode screenshot for cropping'));
    img.src = dataUrl;
  });
}

/**
 * Renders a frame and reads the canvas in the same task, so the WebGPU current texture (or the
 * WebGL2 drawing buffer) still holds it. The crop rect is in canvas (device) pixels.
 */
export async function captureCanvasScreenshot(
  canvas: HTMLCanvasElement,
  module?: BespokeSynthModule | null,
  options: CaptureScreenshotOptions = {},
): Promise<string> {
  if (options.renderFirst !== false) module?._bespoke_render();
  const dataUrl = canvas.toDataURL('image/png');
  const fullWidth = canvas.width;
  const fullHeight = canvas.height;

  const cropped = await cropDataUrl(dataUrl, options, fullWidth, fullHeight);

  const backend: RendererBackend = module?._bespoke_get_renderer_backend() === 1 ? 'webgl' : 'webgpu';

  if (screenshotCallback) {
    screenshotCallback(cropped, {
      width: options.width ?? fullWidth,
      height: options.height ?? fullHeight,
      backend,
    });
  }

  return cropped;
}

export function downloadScreenshot(dataUrl: string, filename = 'bespokesynth-screenshot.png'): void {
  const link = document.createElement('a');
  link.href = dataUrl;
  link.download = filename;
  link.click();
}
