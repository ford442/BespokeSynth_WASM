import type { BespokeSynthApp } from './BespokeSynthApp';
import type { CaptureScreenshotOptions } from '../rendererMode';
import {
  downloadPatchState,
  getPatchStateJson,
  getPlaceholderCount,
  loadBundledLayout,
  loadPatchStateJson,
  promptForPatchStateFile,
} from '../patchState';
import {
  onScreenshotCaptured,
  downloadScreenshot,
  resolveRendererBackend,
  switchRendererPreference,
  type RendererBackend,
} from '../rendererMode';
import { switchAudioPreference, type AudioBackendId } from '../audio/audioMode';
import { runAudioWorkletPoc } from '../audio/workletPoc';
import { downloadOfflineWav, renderOfflineWav } from '../samples/offlineExport';
import { decodePatchFragment, writeShareUrl } from '../samples/patchShare';
import { importAudioFile } from '../samples/sampleIo';
import { collectSampleHashes } from '../samples/opfsStore';
import { getLastKeyCodes } from './inputBridge';
import { readCString, readJson, withCStrings } from './wasmStrings';
import { loadTuningText, type TuningKind } from '../samples/tuningIo';

export function installPublicApi(app: BespokeSynthApp): void {
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
    midiNoteOn: (channel: number, pitch: number, velocity: number) =>
      app.getModule()?._bespoke_midi_note_on?.(channel, pitch, velocity),
    midiNoteOff: (channel: number, pitch: number) =>
      app.getModule()?._bespoke_midi_note_off?.(channel, pitch),
    midiCc: (channel: number, cc: number, value: number) =>
      app.getModule()?._bespoke_midi_cc?.(channel, cc, value),
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
    /** Modules in the loaded patch that this build only holds as placeholders. */
    getPlaceholderCount: () => getPlaceholderCount(app.getModule()),
    downloadPatch: (filename?: string) => downloadPatchState(app.getModule(), filename),
    loadPatchFile: () => promptForPatchStateFile(app.getModule()),
    loadBundledLayout: (path: string) => loadBundledLayout(app.getModule(), path),
    getAudioHealth: () => app.getAudioHealth(),
    getAudioBackend: () => app.getAudioBackendChoice().backend,
    getAudioBackendChoice: () => app.getAudioBackendChoice(),
    getAudioRingStats: () => app.getAudioRingStats(),
    setAudioBackend: (backend: AudioBackendId) => switchAudioPreference(backend),
    play: () => app.play(),
    stop: () => app.stop(),
    runAudioWorkletPoc: (opts?: { stallMs?: number; settleMs?: number }) => runAudioWorkletPoc(opts),
    /** 'sdf' or 'pixel' (?debugFont=pixel, or the SDF atlas failed to load). */
    getFontMode: () => (app.getModule()?._bespoke_get_font_mode?.() === 1 ? 'pixel' : 'sdf'),
    /** Live analyzer FFT counters: CPU (render thread) vs WebGPU compute dispatches. */
    getAnalyzerStats: () => {
      const mod = app.getModule();
      return {
        cpuFftCount: mod?._bespoke_get_analyzer_cpu_fft_count?.() ?? 0,
        gpuFftCount: mod?._bespoke_get_analyzer_gpu_fft_count?.() ?? 0,
      };
    },
    setRendererBackend: (backend: RendererBackend) => switchRendererPreference(backend),
    getRendererBackend: () => {
      const mod = app.getModule();
      if (mod?._bespoke_get_renderer_backend) {
        return mod._bespoke_get_renderer_backend() === 1 ? 'webgl' : 'webgpu';
      }
      return resolveRendererBackend();
    },
    /** true/1 = canonical scene, 2 = drum bus scene, false/0 = off. */
    setRenderTestMode: (scene: boolean | number) =>
      app.getModule()?._bespoke_set_render_test_mode?.(typeof scene === 'number' ? scene : scene ? 1 : 0),
    getRenderTestMode: () => (app.getModule()?._bespoke_get_render_test_mode?.() ?? 0) > 0,
    getRenderTestScene: () => app.getModule()?._bespoke_get_render_test_mode?.() ?? 0,
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
    getLastKeyCodes,
    // --- Live instrument: undo, MIDI learn/out, QWERTY, tuning, scripts, touch ---
    undo: () => (app.getModule()?._bespoke_undo?.() ?? 0) === 1,
    redo: () => (app.getModule()?._bespoke_redo?.() ?? 0) === 1,
    getUndoDepth: () => app.getModule()?._bespoke_get_undo_depth?.() ?? 0,
    getRedoDepth: () => app.getModule()?._bespoke_get_redo_depth?.() ?? 0,
    setControlValue: (moduleId: number, name: string, value: number) => {
      const mod = app.getModule();
      if (!mod) return;
      withCStrings(mod, [name], (ptr) => mod._bespoke_set_control_value(moduleId, ptr, value));
    },
    getControlValue: (moduleId: number, name: string) => {
      const mod = app.getModule();
      if (!mod) return NaN;
      return withCStrings(mod, [name], (ptr) => mod._bespoke_get_control_value(moduleId, ptr));
    },
    getModuleInfo: (moduleId: number) => {
      const mod = app.getModule();
      return mod ? readJson<Record<string, number | string>>(mod, mod._bespoke_get_module_info_json(moduleId), {}) : {};
    },
    getModules: () => {
      const mod = app.getModule();
      return mod ? readJson<Array<{ id: number; type: string }>>(mod, mod._bespoke_get_modules_json(), []) : [];
    },
    midiLearn: (moduleId: number, control = '') => {
      const mod = app.getModule();
      if (!mod) return false;
      return withCStrings(mod, [control], (ptr) => mod._bespoke_midi_learn_arm(moduleId, ptr)) === 1;
    },
    cancelMidiLearn: () => app.getModule()?._bespoke_midi_learn_cancel?.(),
    getMidiLearnTarget: () => {
      const mod = app.getModule();
      if (!mod) return null;
      const moduleId = mod._bespoke_midi_learn_get_module();
      return moduleId >= 0 ? { moduleId, control: readCString(mod, mod._bespoke_midi_learn_get_control()) } : null;
    },
    getMidiCcMap: () => {
      const mod = app.getModule();
      return mod ? readJson<unknown[]>(mod, mod._bespoke_get_midi_cc_map_json(), []) : [];
    },
    setMidiNoteChannel: (channel: number) => app.getModule()?._bespoke_set_midi_note_channel?.(channel),
    getMidiNoteChannel: () => app.getModule()?._bespoke_get_midi_note_channel?.() ?? -1,
    listMidiOutputs: () => app.live.midiOut.listOutputs(),
    selectMidiOutput: (id: string | null) => app.live.midiOut.selectOutput(id),
    getMidiOutLog: () => app.live.midiOut.getLog(),
    clearMidiOutLog: () => app.live.midiOut.clearLog(),
    setQwertyPiano: (enabled: boolean) => app.live.piano.setEnabled(enabled),
    getQwertyPiano: () => app.live.piano.getState(),
    loadTuning: (kind: TuningKind, text: string, name = '') => {
      const mod = app.getModule();
      return mod ? loadTuningText(mod, kind, text, name) : { ok: false, error: 'not ready' };
    },
    getNoteFrequency: (pitch: number) => app.getModule()?._bespoke_get_note_frequency?.(pitch) ?? 0,
    runScript: (code?: string, moduleId?: number) => app.getScriptPanel()?.run(code, moduleId) ?? -1,
    stopScript: () => app.getScriptPanel()?.stop(),
    getScriptStatus: () => app.getScriptPanel()?.status() ?? 'unavailable',
    getScriptLog: () => app.getScriptPanel()?.log() ?? [],
    getSampleRate: () => app.getModule()?._bespoke_get_sample_rate?.() ?? 44100,
    isSpawnMenuOpen: () => (app.getModule()?._bespoke_is_text_entry_active?.() ?? 0) === 1,
    getTransportBeat: () => {
      app.syncAudioEngine();
      return app.getModule()?._bespoke_get_transport_beat?.() ?? 0;
    },
    /** Latest output samples (analyzer ring), for pitch/level checks. */
    getOutputWaveform: (count = 2048) => {
      const mod = app.getModule();
      if (!mod) return [];
      app.syncAudioEngine();
      const ptr = mod._malloc(count * 4);
      try {
        const copied = mod._bespoke_copy_output_waveform(ptr, count);
        return Array.from(mod.HEAPF32.subarray(ptr >> 2, (ptr >> 2) + copied));
      } finally {
        mod._free(ptr);
      }
    },
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
      const { restorePatchSamples } = await import('../samples/sampleIo');
      await restorePatchSamples(mod, collectSampleHashes(json));
      return loadPatchStateJson(mod, json);
    },
  };
}
