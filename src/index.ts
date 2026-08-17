/**
 * BespokeSynth WASM Web Application Entry Point
 */

import './styles.css';
import type { CaptureScreenshotOptions } from './rendererMode';
import {
  downloadPatchState,
  getPatchStateJson,
  loadBundledLayout,
  loadPatchStateJson,
  promptForPatchStateFile,
} from './patchState';
import {
  onScreenshotCaptured,
  downloadScreenshot,
  resolveRendererBackend,
  switchRendererPreference,
  type RendererBackend,
} from './rendererMode';
import { readAudioHealth } from './audio/audioHealth';
import { resolveAudioBackend, switchAudioPreference, type AudioBackendId } from './audio/audioMode';
import { runAudioWorkletPoc } from './audio/workletPoc';
import { BespokeSynthApp } from './app/BespokeSynthApp';
import { downloadOfflineWav, renderOfflineWav } from './samples/offlineExport';
import { decodePatchFragment, writeShareUrl } from './samples/patchShare';
import { importAudioFile } from './samples/sampleIo';
import { collectSampleHashes } from './samples/opfsStore';

document.addEventListener('DOMContentLoaded', async () => {
  const app = new BespokeSynthApp();

  try {
    await app.init();
  } catch (error) {
    console.error('Application initialization failed:', error);
  }

  (window as unknown as Record<string, unknown>).__bespoke = {
    createModule: (type: string, x: number, y: number) => {
      const mod = app.getModule();
      if (mod?.ccall) {
        return mod.ccall('bespoke_create_module', 'number', ['string', 'number', 'number'], [type, x, y]);
      }
      return -1;
    },
    deleteModule: (id: number) => app.getModule()?._bespoke_delete_module?.(id),
    connectModules: (srcId: number, destId: number) =>
      app.getModule()?._bespoke_connect_modules?.(srcId, destId),
    setViewMode: (mode: number) => app.getModule()?._bespoke_set_view_mode?.(mode),
    getModuleCount: () => app.getModule()?._bespoke_get_module_count?.() ?? 0,
    getControlCount: () => app.getModule()?._bespoke_get_control_count?.() ?? 0,
    getControlInfo: (index: number) => {
      const mod = app.getModule();
      if (!mod?._bespoke_get_control_info) return null;
      const ptr = mod._bespoke_get_control_info(index);
      if (!ptr) return null;
      try {
        return JSON.parse(mod.UTF8ToString(ptr));
      } catch {
        return null;
      }
    },
    setThemeColor: (colorId: number, r: number, g: number, b: number, a = 1.0) =>
      app.getModule()?._bespoke_set_theme_color?.(colorId, r, g, b, a),
    resetTheme: () => app.getModule()?._bespoke_reset_theme?.(),
    captureScreenshot: async (options?: CaptureScreenshotOptions) => app.captureScreenshot(options ?? {}),
    downloadScreenshot,
    onScreenshotCaptured,
    getStateJson: () => getPatchStateJson(app.getModule()),
    loadStateJson: (json: string) => loadPatchStateJson(app.getModule(), json),
    downloadPatch: (filename?: string) => downloadPatchState(app.getModule(), filename),
    loadPatchFile: () => promptForPatchStateFile(app.getModule()),
    loadBundledLayout: (path: string) => loadBundledLayout(app.getModule(), path),
    getAudioHealth: () => readAudioHealth(app.getModule()),
    getAudioBackend: () => resolveAudioBackend(),
    setAudioBackend: (backend: AudioBackendId) => switchAudioPreference(backend),
    play: () => app.play(),
    stop: () => app.stop(),
    runAudioWorkletPoc: (opts?: { stallMs?: number; settleMs?: number }) => runAudioWorkletPoc(opts),
    setRendererBackend: (backend: RendererBackend) => switchRendererPreference(backend),
    getRendererBackend: () => {
      const mod = app.getModule();
      if (mod?._bespoke_get_renderer_backend) {
        return mod._bespoke_get_renderer_backend() === 1 ? 'webgl' : 'webgpu';
      }
      return resolveRendererBackend();
    },
    setRenderTestMode: (enabled: boolean) =>
      app.getModule()?._bespoke_set_render_test_mode?.(enabled ? 1 : 0),
    getRenderTestMode: () => app.getModule()?._bespoke_get_render_test_mode?.() === 1,
    setWebGLDebugMode: (mode: number) => app.getModule()?._bespoke_set_webgl_debug_mode?.(mode),
    setFontTestVisible: (visible: boolean) => {
      const mod = app.getModule();
      mod?._bespoke_set_font_test_visible?.(visible ? 1 : 0);
      mod?._bespoke_render?.();
    },
    renderFrame: () => app.getModule()?._bespoke_render?.(),
    getHostRenderCount: () => app.getModule()?._bespoke_get_host_render_count?.() ?? 0,
    getHostMouseDownCount: () => app.getModule()?._bespoke_get_host_mouse_down_count?.() ?? 0,
    resetHostCounters: () => app.getModule()?._bespoke_reset_host_counters?.(),
    getHostFrameCount: () => app.getHostFrameCount(),
    importSampleFile: (file: File) => {
      const mod = app.getModule();
      return mod ? importAudioFile(mod, file) : Promise.resolve(-1);
    },
    renderOfflineWav: (options?: { seconds?: number; sampleRate?: number; bitsPerSample?: 16 | 24 | 32 }) => {
      const mod = app.getModule();
      return mod ? renderOfflineWav(mod, options) : null;
    },
    downloadOfflineWav: (options?: { seconds?: number; sampleRate?: number }) => {
      const mod = app.getModule();
      return mod ? downloadOfflineWav(mod, options) : false;
    },
    sharePatch: async () => {
      const json = getPatchStateJson(app.getModule());
      return writeShareUrl(json);
    },
    loadSharedPatch: async (hash?: string) => {
      const json = await decodePatchFragment(hash ?? window.location.hash);
      if (!json) return false;
      const mod = app.getModule();
      if (!mod) return false;
      const { restorePatchSamples } = await import('./samples/sampleIo');
      await restorePatchSamples(mod, collectSampleHashes(json));
      return loadPatchStateJson(mod, json);
    },
  };

  window.addEventListener('beforeunload', () => {
    app.shutdown();
  });
});

export { BespokeSynthApp };
