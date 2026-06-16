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

function rgbaToPngDataUrl(rgba: Uint8Array, width: number, height: number): string {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d');
  if (!ctx) throw new Error('2D canvas context unavailable for PNG encoding');

  const imageData = ctx.createImageData(width, height);
  imageData.data.set(rgba);
  ctx.putImageData(imageData, 0, 0);
  return canvas.toDataURL('image/png');
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

function readWasmScreenshot(module: any): { rgba: Uint8Array; width: number; height: number } | null {
  if (!module?._bespoke_capture_screenshot || !module?._bespoke_get_screenshot_pixels) return null;

  const widthPtr = module._malloc(4);
  const heightPtr = module._malloc(4);
  try {
    const backendCode = module._bespoke_capture_screenshot(widthPtr, heightPtr);
    const width = module.HEAP32[widthPtr >> 2];
    const height = module.HEAP32[heightPtr >> 2];

    if (backendCode === 1) {
      return null; // WebGL2 — use canvas readback
    }
    if (backendCode !== 0 || width <= 0 || height <= 0) {
      return null;
    }

    const byteLenPtr = module._malloc(4);
    try {
      const ptr = module._bespoke_get_screenshot_pixels(byteLenPtr);
      const byteLen = module.HEAP32[byteLenPtr >> 2];
      if (!ptr || byteLen <= 0) return null;
      const rgba = new Uint8Array(module.HEAPU8.buffer, ptr, byteLen).slice();
      return { rgba, width, height };
    } finally {
      module._free(byteLenPtr);
    }
  } finally {
    module._free(widthPtr);
    module._free(heightPtr);
  }
}

export async function captureCanvasScreenshot(
  canvas: HTMLCanvasElement,
  module?: any,
  options: CaptureScreenshotOptions = {},
): Promise<string> {
  let dataUrl: string;
  let fullWidth = canvas.width;
  let fullHeight = canvas.height;

  const wasmPixels = module ? readWasmScreenshot(module) : null;
  if (wasmPixels) {
    dataUrl = rgbaToPngDataUrl(wasmPixels.rgba, wasmPixels.width, wasmPixels.height);
    fullWidth = wasmPixels.width;
    fullHeight = wasmPixels.height;
  } else {
    if (options.renderFirst !== false && module?._bespoke_capture_screenshot) {
      const widthPtr = module._malloc(4);
      const heightPtr = module._malloc(4);
      try {
        module._bespoke_capture_screenshot(widthPtr, heightPtr);
        fullWidth = module.HEAP32[widthPtr >> 2];
        fullHeight = module.HEAP32[heightPtr >> 2];
      } finally {
        module._free(widthPtr);
        module._free(heightPtr);
      }
    } else if (options.renderFirst !== false && module?._bespoke_render) {
      module._bespoke_render();
    }
    dataUrl = canvas.toDataURL('image/png');
  }

  const cropped = await cropDataUrl(dataUrl, options, fullWidth, fullHeight);

  const backend: RendererBackend =
    module?._bespoke_get_renderer_backend?.() === 1 ? 'webgl' : 'webgpu';

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
