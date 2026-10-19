import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import {
  switchRendererPreference,
  type RendererBackend,
} from '../rendererMode';
import {
  switchAudioPreference,
  type AudioBackendChoice,
  type AudioBackendId,
} from '../audio/audioMode';
import { setupMidiPanel } from './midiPanel';
import type { LiveInstrument } from '../app/liveInstrument';

export interface RendererPanelOptions {
  rendererBackend: RendererBackend;
  audioBackendChoice: AudioBackendChoice;
  getModule: () => BespokeSynthModule | null;
  onScreenshot: () => void;
  onSavePatch: () => void;
  onLoadPatch: () => void;
  onImportSample?: () => void;
  onExportWav?: () => void;
  onSharePatch?: () => void;
  onToggleInput?: () => void;
  live?: LiveInstrument;
}

export function setupRendererPanel(options: RendererPanelOptions): void {
  const headerControls = document.querySelector('#header .controls');
  if (!headerControls) return;

  const rendererSelect = document.createElement('select');
  rendererSelect.id = 'rendererSelect';
  rendererSelect.className = 'renderer-select';
  rendererSelect.innerHTML = `
    <option value="webgpu">WebGPU</option>
    <option value="webgl">WebGL2</option>
  `;
  rendererSelect.value = options.rendererBackend;
  rendererSelect.title = 'Renderer backend (reloads page)';
  rendererSelect.addEventListener('change', () => {
    switchRendererPreference(rendererSelect.value as RendererBackend);
  });

  const audioSelect = document.createElement('select');
  audioSelect.id = 'audioSelect';
  audioSelect.className = 'renderer-select';
  // The tone POC (?audioWorkletPoc=1) is a diagnostic and deliberately not listed here.
  audioSelect.innerHTML = `
    <option value="worklet">Audio: Worklet</option>
    <option value="ring">Audio: Worklet ring (main-thread DSP)</option>
    <option value="sdl">Audio: SDL2</option>
  `;
  const choice = options.audioBackendChoice;
  audioSelect.value = choice.backend;
  if (choice.fallbackReason) {
    for (const value of ['worklet', 'ring']) {
      const option = audioSelect.querySelector<HTMLOptionElement>(`option[value="${value}"]`);
      if (option) {
        option.disabled = true;
        option.textContent += ' (unavailable)';
      }
    }
    const sdlOption = audioSelect.querySelector<HTMLOptionElement>('option[value="sdl"]');
    if (sdlOption) sdlOption.textContent = 'Audio: SDL2 (fallback)';
    audioSelect.title = `AudioWorklet unavailable: ${choice.fallbackReason}. Using SDL2.`;
  } else {
    audioSelect.title =
      'Audio backend (reloads page). Worklet (the graph runs in the AudioWorklet) is the default when the page ' +
      'is cross-origin isolated; the ring keeps the graph on the main thread.';
  }
  audioSelect.addEventListener('change', () => {
    switchAudioPreference(audioSelect.value as AudioBackendId);
  });

  const debugSelect = document.createElement('select');
  debugSelect.id = 'webglDebugSelect';
  debugSelect.className = 'renderer-select';
  debugSelect.title = 'WebGL2 debug mode';
  debugSelect.innerHTML = `
    <option value="0">Normal</option>
    <option value="1">Wireframe</option>
    <option value="2">Connection debug</option>
    <option value="3">Simplified modules</option>
  `;
  debugSelect.disabled = options.rendererBackend !== 'webgl';
  debugSelect.addEventListener('change', () => {
    const mode = Number(debugSelect.value);
    options.getModule()?._bespoke_set_webgl_debug_mode?.(mode);
  });

  const screenshotBtn = document.createElement('button');
  screenshotBtn.id = 'screenshotBtn';
  screenshotBtn.className = 'btn';
  screenshotBtn.textContent = 'Screenshot';
  screenshotBtn.title = 'Capture canvas PNG (Ctrl+Shift+S)';
  screenshotBtn.addEventListener('click', () => options.onScreenshot());

  setupMidiPanel(headerControls, options.getModule, options.live);

  // Undo/redo mirror Ctrl+Z / Ctrl+Shift+Z for touch and mouse users.
  const undoBtn = document.createElement('button');
  undoBtn.id = 'undoBtn';
  undoBtn.className = 'btn';
  undoBtn.textContent = '↶';
  undoBtn.title = 'Undo (Ctrl+Z)';
  undoBtn.setAttribute('aria-label', 'Undo');
  undoBtn.addEventListener('click', () => options.getModule()?._bespoke_undo?.());

  const redoBtn = document.createElement('button');
  redoBtn.id = 'redoBtn';
  redoBtn.className = 'btn';
  redoBtn.textContent = '↷';
  redoBtn.title = 'Redo (Ctrl+Shift+Z)';
  redoBtn.setAttribute('aria-label', 'Redo');
  redoBtn.addEventListener('click', () => options.getModule()?._bespoke_redo?.());

  const saveBtn = document.createElement('button');
  saveBtn.id = 'savePatchBtn';
  saveBtn.className = 'btn';
  saveBtn.textContent = 'Save';
  saveBtn.title = 'Save patch JSON';
  saveBtn.addEventListener('click', () => options.onSavePatch());

  const loadBtn = document.createElement('button');
  loadBtn.id = 'loadPatchBtn';
  loadBtn.className = 'btn';
  loadBtn.textContent = 'Load';
  loadBtn.title = 'Load patch JSON';
  loadBtn.addEventListener('click', () => options.onLoadPatch());

  const sampleBtn = document.createElement('button');
  sampleBtn.id = 'sampleBtn';
  sampleBtn.className = 'btn';
  sampleBtn.textContent = 'Sample';
  sampleBtn.title = 'Import WAV/FLAC/MP3, or a .scl/.kbm tuning for the Scale module';
  sampleBtn.addEventListener('click', () => options.onImportSample?.());

  const exportBtn = document.createElement('button');
  exportBtn.id = 'exportWavBtn';
  exportBtn.className = 'btn';
  exportBtn.textContent = 'Export WAV';
  exportBtn.title = 'Offline-render the patch to a WAV file';
  exportBtn.addEventListener('click', () => options.onExportWav?.());

  const shareBtn = document.createElement('button');
  shareBtn.id = 'sharePatchBtn';
  shareBtn.className = 'btn';
  shareBtn.textContent = 'Share';
  shareBtn.title = 'Copy a URL that reloads this patch';
  shareBtn.addEventListener('click', () => options.onSharePatch?.());

  const inputBtn = document.createElement('button');
  inputBtn.id = 'inputCaptureBtn';
  inputBtn.className = 'btn';
  inputBtn.textContent = 'Mic';
  inputBtn.title = 'Capture microphone input for the looper';
  inputBtn.addEventListener('click', () => options.onToggleInput?.());

  headerControls.append(
    undoBtn,
    redoBtn,
    rendererSelect,
    audioSelect,
    debugSelect,
    screenshotBtn,
    saveBtn,
    loadBtn,
    sampleBtn,
    exportBtn,
    shareBtn,
    inputBtn,
  );
}

export function setupFloatingRendererToggle(rendererBackend: RendererBackend): void {
  const btn = document.createElement('button');
  btn.id = 'rendererFab';
  btn.className = 'renderer-fab';
  btn.textContent = rendererBackend === 'webgl' ? 'GL2' : 'GPU';
  btn.title = 'Toggle renderer backend (reloads page)';
  btn.addEventListener('click', () => {
    switchRendererPreference(rendererBackend === 'webgl' ? 'webgpu' : 'webgl');
  });
  document.body.appendChild(btn);
}

export function setupKeyboardShortcuts(
  rendererBackend: RendererBackend,
  onScreenshot: () => void,
): void {
  window.addEventListener('keydown', (event) => {
    if (!event.ctrlKey || !event.shiftKey) return;

    if (event.code === 'KeyS') {
      event.preventDefault();
      onScreenshot();
    } else if (event.code === 'KeyR') {
      event.preventDefault();
      switchRendererPreference(rendererBackend === 'webgl' ? 'webgpu' : 'webgl');
    }
  });
}
