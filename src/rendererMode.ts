export type RendererBackend = 'webgpu' | 'webgl';

const STORAGE_KEY = 'bespokesynth.renderer';

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

export async function captureCanvasScreenshot(canvas: HTMLCanvasElement): Promise<string> {
  return canvas.toDataURL('image/png');
}

export function downloadScreenshot(dataUrl: string, filename = 'bespokesynth-screenshot.png'): void {
  const link = document.createElement('a');
  link.href = dataUrl;
  link.download = filename;
  link.click();
}
