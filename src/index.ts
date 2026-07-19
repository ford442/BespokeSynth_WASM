/**
 * BespokeSynth WASM Web Application Entry Point
 * 
 * This is the main TypeScript entry point for the npm-buildable web app.
 * It loads the WASM module and sets up the BespokeSynth interface.
 */

import './styles.css';
import type { BespokeSynthModule } from '../wasm/types/bespoke-synth';
import {
  resolveRendererBackend,
  publishRendererBreadcrumbs,
  switchRendererPreference,
  captureCanvasScreenshot,
  downloadScreenshot,
  isRenderTestRequested,
  onScreenshotCaptured,
  type RendererBackend,
  type CaptureScreenshotOptions,
} from './rendererMode';
import {
  downloadPatchState,
  getPatchStateJson,
  loadBundledLayout,
  loadPatchStateJson,
  promptForPatchStateFile,
} from './patchState';
import { PatchStorage, type StoredPatch } from './patchStorage';
import { connectWebMidi } from './midi';

interface BespokeSynthFactoryConfig {
  canvas: HTMLCanvasElement | HTMLElement | null;
  print: (text: unknown) => void;
  printErr: (text: unknown) => void;
  locateFile?: (path: string, scriptDirectory: string) => string;
}

const WASM_SCRIPT_PATH = 'wasm/BespokeSynthWASM.js';

/** Resolve Emscripten companion assets (.wasm, .data) next to the injected script. */
const resolveWasmAssetUrl = (scriptElement: HTMLScriptElement, file: string): string =>
  new URL(file, new URL(scriptElement.src, document.baseURI)).href;

type BespokeSynthFactory = (config: BespokeSynthFactoryConfig) => Promise<BespokeSynthModule>;

interface BespokeBrowserWindow extends Window {
  createBespokeSynth?: BespokeSynthFactory;
  Module?: BespokeSynthModule;
  __bespoke_on_init_progress?: (step: string, detail: string) => void;
  __bespoke_on_init_complete?: (status: number) => void;
}

const bespokeWindow = window as BespokeBrowserWindow;

// Initialization step definitions
interface InitStep {
  id: string;
  label: string;
  weight: number; // Contribution to total progress (0-100)
}

// Define all initialization steps with their approximate weights
const WEBGPU_INIT_STEPS: InitStep[] = [
  { id: 'wasm_load', label: 'Loading WebAssembly module', weight: 10 },
  { id: 'webgpu_instance', label: 'Creating WebGPU instance', weight: 15 },
  { id: 'webgpu_surface', label: 'Creating WebGPU surface', weight: 10 },
  { id: 'webgpu_adapter', label: 'Requesting GPU adapter', weight: 15 },
  { id: 'webgpu_device', label: 'Acquiring GPU device', weight: 15 },
  { id: 'renderer_pipelines', label: 'Compiling shader pipelines', weight: 20 },
  { id: 'audio_init', label: 'Initializing audio backend', weight: 10 },
  { id: 'controls_create', label: 'Creating UI controls', weight: 5 },
];

const WEBGL2_INIT_STEPS: InitStep[] = [
  { id: 'wasm_load', label: 'Loading WebAssembly module', weight: 10 },
  { id: 'webgl_context', label: 'Creating WebGL2 context', weight: 25 },
  { id: 'webgl_capabilities', label: 'Querying GL capabilities', weight: 15 },
  { id: 'renderer_pipelines', label: 'Compiling GLSL shader programs', weight: 25 },
  { id: 'audio_init', label: 'Initializing audio backend', weight: 15 },
  { id: 'controls_create', label: 'Creating UI controls', weight: 10 },
];

// Load WASM module script dynamically
const loadWasmModule = async (canvas?: HTMLCanvasElement): Promise<BespokeSynthModule> => {
  return new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = WASM_SCRIPT_PATH;
    script.onload = async () => {
      console.log('loadWasmModule: script loaded');
      const locateWasmAsset = (path: string) => resolveWasmAssetUrl(script, path);
      // If the build was modularized, a factory function will be exposed
      const factory = bespokeWindow.createBespokeSynth;
      console.log('loadWasmModule: factory type =', typeof factory);
      if (typeof factory === 'function') {
        const config: BespokeSynthFactoryConfig = {
          canvas: canvas ?? document.getElementById('canvas'),
          print: (text: unknown) => console.log(text),
          printErr: (text: unknown) => console.error(text),
          locateFile: locateWasmAsset,
        };

        try {
          console.log('loadWasmModule: invoking factory to create module instance');
          const instance = await factory(config);
          console.log('loadWasmModule: factory resolved an instance');
          resolve(instance);
          return;
        } catch (err) {
          console.error('loadWasmModule: factory threw error', err);
          reject(err);
          return;
        }
      }

      // Fallback for non-modularized builds (legacy global Module)
      if (bespokeWindow.Module?.calledRun) {
        resolve(bespokeWindow.Module);
      } else {
        bespokeWindow.Module = {
          ...bespokeWindow.Module,
          locateFile: locateWasmAsset,
          onRuntimeInitialized: () => {
            console.log('loadWasmModule: onRuntimeInitialized called — resolving Module');
            if (bespokeWindow.Module) {
              resolve(bespokeWindow.Module);
            } else {
              reject(new Error('WASM Module global was not initialized'));
            }
          },
        } as BespokeSynthModule;
      }
    };
    script.onerror = () => reject(new Error('Failed to load WASM module'));
    document.head.appendChild(script);
  });
};

// Main application class
class BespokeSynthApp {
  private canvas: HTMLCanvasElement | null = null;
  private module: BespokeSynthModule | null = null;
  private animationFrameId: number | null = null;
  private isInitialized = false;
  private currentProgress = 0;
  private completedSteps = new Set<string>();
  private activeStep: string | null = null;
  private initStartTime = 0;
  private isProcessingEvents = false;
  private readonly patchStorage = new PatchStorage();
  private selectedPatchId: string | undefined;
  private autoSaveTimer: number | null = null;
  private lastAutoSaveJson = '';

  // Hybrid DOM text overlay
  private guiOverlay: HTMLElement | null = null;
  // Reusable label elements keyed by control index
  private labelElements: Map<number, HTMLElement> = new Map();
  private rendererBackend: RendererBackend = resolveRendererBackend();
  private rendererFallbackReason: string | null = null;
  private initSteps: InitStep[] =
    resolveRendererBackend() === 'webgl' ? WEBGL2_INIT_STEPS : WEBGPU_INIT_STEPS;

  getModule(): BespokeSynthModule | null {
    return this.module;
  }

  async init(): Promise<void> {
    this.initStartTime = performance.now();
    console.log('Initializing BespokeSynth WASM...');

    // Get canvas element
    this.canvas = document.getElementById('canvas') as HTMLCanvasElement;
    if (!this.canvas) {
      throw new Error('Canvas element not found');
    }

    // Set canvas size
    this.resizeCanvas();
    window.addEventListener('resize', () => this.resizeCanvas());

    // Initialize progress UI
    this.initSteps = this.rendererBackend === 'webgl' ? WEBGL2_INIT_STEPS : WEBGPU_INIT_STEPS;
    this.initializeProgressUI();
    this.setupInitProgressCallback();
    this.setupRendererDebugUI();
    this.setupPatchStorageUI();
    this.setupKeyboardShortcuts();

    const statusSubheader = document.querySelector('#status .status-subheader');
    if (statusSubheader) {
      statusSubheader.textContent =
        this.rendererBackend === 'webgl'
          ? 'Setting up WebGL2 and audio...'
          : 'Setting up WebGPU and audio...';
    }

    try {
      // Step 1: Load WASM module
      this.setActiveStep('wasm_load');
      this.module = await loadWasmModule(this.canvas ?? undefined);
      this.completeStep('wasm_load');
      console.log('WASM module loaded successfully');

      // Select renderer backend before C++ init
      const backendCode = this.rendererBackend === 'webgl' ? 1 : 0;
      this.module._bespoke_set_renderer_backend?.(backendCode);
      publishRendererBreadcrumbs(this.rendererBackend, this.rendererFallbackReason);

      if (this.rendererBackend === 'webgl') {
        const webglSupported = this.module._bespoke_is_webgl2_supported?.() === 1;
        if (!webglSupported) {
          const detail = this.module.UTF8ToString?.(
            this.module._bespoke_get_webgl2_error?.() ?? 0,
          );
          throw new Error(
            detail ||
              'WebGL2 is not available in this browser. Try ?renderer=webgpu or use Chrome/Edge/Firefox.',
          );
        }
      }

      // Initialize synth - this will trigger async WebGPU initialization
      const sampleRate = 44100;
      const bufferSize = 512;
      
      // Poll for init state updates from C++
      const statePollInterval = window.setInterval(() => {
        this.pollInitState();
      }, 50);

      const result = this.module._bespoke_init?.(
        this.canvas.width,
        this.canvas.height,
        sampleRate,
        bufferSize
      );

      if (result === 0) {
        // Synchronous initialization (rare but possible)
        clearInterval(statePollInterval);
        this.completeAllSteps();
        this.isInitialized = true;
        publishRendererBreadcrumbs(
          this.module._bespoke_get_renderer_backend?.() === 1 ? 'webgl' : 'webgpu',
          this.rendererFallbackReason,
        );
        this.showReadyState();
        this.setupEventListeners();
        this.startRenderLoop();
        this.applyPostInitOptions();
        console.log('BespokeSynth initialized successfully (sync)');
      } else if (result === 1) {
        // Initialization is pending asynchronously
        try {
          await this.waitForAsyncInit(statePollInterval);
        } catch (asyncError) {
          const canFallback =
            this.rendererBackend === 'webgpu' &&
            !this.wasWebGPUExplicitlyRequested() &&
            this.module?._bespoke_shutdown;

          if (canFallback && await this.retryWithWebGL2(sampleRate, bufferSize)) {
            clearInterval(statePollInterval);
            this.completeAllSteps();
            this.isInitialized = true;
            publishRendererBreadcrumbs('webgl', this.rendererFallbackReason);
            this.showReadyState();
            this.setupEventListeners();
            this.startRenderLoop();
            this.applyPostInitOptions();
            console.log('BespokeSynth initialized with WebGL2 fallback');
            return;
          }
          throw asyncError;
        }
      } else {
        throw new Error(`Initialization failed with code: ${result}`);
      }
    } catch (error) {
      console.error('Failed to initialize BespokeSynth:', error);
      this.showErrorState(error instanceof Error ? error.message : 'Unknown error');
    }
  }

  private wasWebGPUExplicitlyRequested(): boolean {
    const params = new URLSearchParams(window.location.search);
    const explicit = params.get('renderer')?.toLowerCase();
    return explicit === 'webgpu' || params.has('webgpu');
  }

  private async retryWithWebGL2(sampleRate: number, bufferSize: number): Promise<boolean> {
    if (!this.canvas || !this.module) return false;

    console.warn('WebGPU init failed; retrying with WebGL2 fallback...');
    this.module._bespoke_shutdown?.();
    this.rendererBackend = 'webgl';
    this.rendererFallbackReason = 'WebGPU initialization failed; fell back to WebGL2';
    this.initSteps = WEBGL2_INIT_STEPS;
    this.initializeProgressUI();
    this.module._bespoke_set_renderer_backend?.(1);
    publishRendererBreadcrumbs(this.rendererBackend, this.rendererFallbackReason);

    const debugSelect = document.getElementById('webglDebugSelect') as HTMLSelectElement | null;
    if (debugSelect) debugSelect.disabled = false;

    const result = this.module._bespoke_init?.(
      this.canvas.width,
      this.canvas.height,
      sampleRate,
      bufferSize,
    );
    return result === 0;
  }

  private setupRendererDebugUI(): void {
    const headerControls = document.querySelector('#header .controls');
    if (!headerControls) return;

    const rendererSelect = document.createElement('select');
    rendererSelect.id = 'rendererSelect';
    rendererSelect.className = 'renderer-select';
    rendererSelect.innerHTML = `
      <option value="webgpu">WebGPU</option>
      <option value="webgl">WebGL2</option>
    `;
    rendererSelect.value = this.rendererBackend;
    rendererSelect.title = 'Renderer backend (reloads page)';
    rendererSelect.addEventListener('change', () => {
      switchRendererPreference(rendererSelect.value as RendererBackend);
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
    debugSelect.disabled = this.rendererBackend !== 'webgl';
    debugSelect.addEventListener('change', () => {
      const mode = Number(debugSelect.value);
      this.module?._bespoke_set_webgl_debug_mode?.(mode);
    });

    const screenshotBtn = document.createElement('button');
    screenshotBtn.id = 'screenshotBtn';
    screenshotBtn.className = 'btn';
    screenshotBtn.textContent = 'Screenshot';
    screenshotBtn.title = 'Capture canvas PNG (Ctrl+Shift+S)';
    screenshotBtn.addEventListener('click', () => void this.captureScreenshot());

    const midiBtn = document.createElement('button');
    midiBtn.id = 'midiBtn'; midiBtn.className = 'btn'; midiBtn.textContent = 'MIDI';
    midiBtn.title = 'Connect browser MIDI inputs';
    midiBtn.addEventListener('click', async () => {
      if (!this.module) return;
      try { midiBtn.textContent = `MIDI ${await connectWebMidi(this.module)}`; }
      catch (error) { console.error('Web MIDI connection failed:', error); midiBtn.textContent = 'MIDI unavailable'; }
    });

    const saveBtn = document.createElement('button');
    saveBtn.id = 'savePatchBtn';
    saveBtn.className = 'btn';
    saveBtn.textContent = 'Save';
    saveBtn.title = 'Save patch JSON';
    saveBtn.addEventListener('click', () => {
      if (this.module) downloadPatchState(this.module);
    });

    const loadBtn = document.createElement('button');
    loadBtn.id = 'loadPatchBtn';
    loadBtn.className = 'btn';
    loadBtn.textContent = 'Load';
    loadBtn.title = 'Load patch JSON';
    loadBtn.addEventListener('click', () => {
      if (this.module) void promptForPatchStateFile(this.module);
    });

    headerControls.appendChild(rendererSelect);
    headerControls.appendChild(debugSelect);
    headerControls.appendChild(screenshotBtn);
    headerControls.appendChild(midiBtn);
    headerControls.appendChild(saveBtn);
    headerControls.appendChild(loadBtn);

    if (new URLSearchParams(window.location.search).has('debug')) {
      this.setupFloatingRendererToggle();
    }
  }

  private setupPatchStorageUI(): void {
    const controls = document.querySelector('#header .controls');
    if (!controls) return;
    const select = document.createElement('select');
    select.id = 'patchSelect'; select.className = 'renderer-select'; select.title = 'Saved browser patches';
    const save = document.createElement('button'); save.className = 'btn'; save.textContent = 'Save browser';
    const load = document.createElement('button'); load.className = 'btn'; load.textContent = 'Load browser';
    const rename = document.createElement('button'); rename.className = 'btn'; rename.textContent = 'Rename';
    const remove = document.createElement('button'); remove.className = 'btn'; remove.textContent = 'Delete';
    const auto = document.createElement('label'); auto.className = 'autosave-toggle';
    const checkbox = document.createElement('input'); checkbox.type = 'checkbox'; checkbox.id = 'autosaveToggle';
    auto.append(checkbox, document.createTextNode(' Auto-save'));
    const refresh = async (): Promise<StoredPatch[]> => {
      const patches = await this.patchStorage.list();
      select.replaceChildren(new Option('Saved patches', ''));
      for (const patch of patches) select.add(new Option(patch.name, patch.id));
      select.value = this.selectedPatchId ?? '';
      return patches;
    };
    save.addEventListener('click', async () => {
      const name = window.prompt('Patch name', select.selectedOptions[0]?.textContent ?? 'Untitled patch');
      if (name === null) return;
      const patch = await this.patchStorage.save(name, getPatchStateJson(this.module), this.selectedPatchId);
      this.selectedPatchId = patch.id; await refresh();
    });
    load.addEventListener('click', async () => {
      const patch = (await this.patchStorage.list()).find((item) => item.id === select.value);
      if (patch && loadPatchStateJson(this.module, patch.json)) { this.selectedPatchId = patch.id; this.lastAutoSaveJson = patch.json; }
    });
    rename.addEventListener('click', async () => {
      const patch = (await this.patchStorage.list()).find((item) => item.id === select.value);
      if (!patch) return;
      const name = window.prompt('Patch name', patch.name);
      if (name !== null) { await this.patchStorage.save(name, patch.json, patch.id); await refresh(); }
    });
    remove.addEventListener('click', async () => { if (select.value) { await this.patchStorage.remove(select.value); this.selectedPatchId = undefined; await refresh(); } });
    checkbox.addEventListener('change', () => this.configureAutoSave(checkbox.checked));
    controls.append(select, save, load, rename, remove, auto);
    void refresh();
  }

  private configureAutoSave(enabled: boolean): void {
    if (this.autoSaveTimer !== null) window.clearInterval(this.autoSaveTimer);
    this.autoSaveTimer = enabled ? window.setInterval(() => {
      const json = getPatchStateJson(this.module);
      if (json === this.lastAutoSaveJson) return;
      this.lastAutoSaveJson = json;
      void this.patchStorage.save('Auto-saved patch', json, this.selectedPatchId).then((patch) => { this.selectedPatchId = patch.id; });
    }, 1500) : null;
  }

  private setupFloatingRendererToggle(): void {
    const btn = document.createElement('button');
    btn.id = 'rendererFab';
    btn.className = 'renderer-fab';
    btn.textContent = this.rendererBackend === 'webgl' ? 'GL2' : 'GPU';
    btn.title = 'Toggle renderer backend (reloads page)';
    btn.addEventListener('click', () => {
      switchRendererPreference(this.rendererBackend === 'webgl' ? 'webgpu' : 'webgl');
    });
    document.body.appendChild(btn);
  }

  private setupKeyboardShortcuts(): void {
    window.addEventListener('keydown', (event) => {
      if (!event.ctrlKey || !event.shiftKey) return;

      if (event.code === 'KeyS') {
        event.preventDefault();
        void this.captureScreenshot();
      } else if (event.code === 'KeyR') {
        event.preventDefault();
        switchRendererPreference(this.rendererBackend === 'webgl' ? 'webgpu' : 'webgl');
      }
    });
  }

  private applyPostInitOptions(): void {
    if (!this.module) return;

    if (isRenderTestRequested()) {
      this.module._bespoke_set_render_test_mode?.(1);
      console.log('Render test mode: canonical scene loaded (?renderTest=1)');
    }

    if (new URLSearchParams(window.location.search).get('patch') === 'starter') {
      if (!loadBundledLayout(this.module, 'savestate/wasm-starter.bsk')) {
        console.warn('Bundled starter patch could not be loaded');
      }
    }

    const activeBackend = this.module._bespoke_get_renderer_backend?.() === 1 ? 'webgl' : 'webgpu';
    this.rendererBackend = activeBackend;
    publishRendererBreadcrumbs(activeBackend, this.rendererFallbackReason);

    const debugSelect = document.getElementById('webglDebugSelect') as HTMLSelectElement | null;
    if (debugSelect) debugSelect.disabled = activeBackend !== 'webgl';

    const rendererSelect = document.getElementById('rendererSelect') as HTMLSelectElement | null;
    if (rendererSelect) rendererSelect.value = activeBackend;
  }

  async captureScreenshot(options: CaptureScreenshotOptions = {}): Promise<string | null> {
    if (!this.canvas) return null;
    try {
      const dataUrl = await captureCanvasScreenshot(this.canvas, this.module, options);
      if (!options.x && !options.y && !options.width && !options.height) {
        downloadScreenshot(dataUrl);
      }
      return dataUrl;
    } catch (error) {
      console.error('Screenshot capture failed:', error);
      return null;
    }
  }

  private setupInitProgressCallback(): void {
    bespokeWindow.__bespoke_on_init_progress = (step: string, detail: string) => {
      console.log(`[InitProgress] ${step}: ${detail}`);

      const progressToStep: Record<string, string> = {
        init_start: 'wasm_load',
        webgpu_requested: 'webgpu_instance',
        webgpu_ready: 'webgpu_adapter',
        webgl_requested: 'webgl_context',
        webgl_ready: 'webgl_capabilities',
        renderer_init: 'renderer_pipelines',
        renderer_ready: 'renderer_pipelines',
        audio_init: 'audio_init',
        audio_ready: 'audio_init',
        controls_init: 'controls_create',
        init_complete: 'controls_create',
      };

      const mapped = progressToStep[step];
      if (mapped) {
        this.setActiveStep(mapped);
      }

      if (step.endsWith('_ready') || step === 'init_complete') {
        if (mapped) {
          this.completeStep(mapped);
        }
      }

      if (step.endsWith('_failed') || step === 'webgpu_start_failed') {
        const subheader = document.querySelector('#status .status-subheader');
        if (subheader) {
          subheader.textContent = detail;
        }
      }
    };
  }

  private initializeProgressUI(): void {
    const stepsContainer = document.getElementById('init-steps');
    if (!stepsContainer) return;

    stepsContainer.innerHTML = '';
    this.initSteps.forEach((step) => {
      const stepEl = document.createElement('div');
      stepEl.className = 'init-step';
      stepEl.id = `step-${step.id}`;
      stepEl.innerHTML = `
        <span class="init-step-icon">○</span>
        <span class="init-step-text">${step.label}</span>
      `;
      stepsContainer.appendChild(stepEl);
    });
  }

  private setActiveStep(stepId: string): void {
    this.activeStep = stepId;
    
    // Update UI
    const stepEl = document.getElementById(`step-${stepId}`);
    if (stepEl) {
      stepEl.classList.add('active');
      stepEl.querySelector('.init-step-icon')!.textContent = '◌';
    }

    // Calculate progress based on completed steps + current step
    this.updateProgress();
    
    console.log(`[Init] Starting step: ${stepId}`);
  }

  private completeStep(stepId: string): void {
    this.completedSteps.add(stepId);
    this.activeStep = null;

    // Update UI
    const stepEl = document.getElementById(`step-${stepId}`);
    if (stepEl) {
      stepEl.classList.remove('active');
      stepEl.classList.add('completed');
      stepEl.querySelector('.init-step-icon')!.textContent = '✓';
    }

    this.updateProgress();
    
    const elapsed = ((performance.now() - this.initStartTime) / 1000).toFixed(2);
    console.log(`[Init] Completed step: ${stepId} (${elapsed}s)`);
  }

  private completeAllSteps(): void {
    this.initSteps.forEach(step => this.completeStep(step.id));
  }

  private updateProgress(): void {
    let progress = 0;

    // Add completed steps
    this.initSteps.forEach(step => {
      if (this.completedSteps.has(step.id)) {
        progress += step.weight;
      }
    });

    // Add partial progress for active step (50% of its weight)
    if (this.activeStep) {
      const activeStepData = this.initSteps.find(s => s.id === this.activeStep);
      if (activeStepData) {
        progress += activeStepData.weight * 0.5;
      }
    }

    this.currentProgress = Math.min(100, Math.round(progress));
    
    const fillEl = document.getElementById('progress-fill');
    const textEl = document.getElementById('progress-text');
    
    if (fillEl) fillEl.style.width = `${this.currentProgress}%`;
    if (textEl) textEl.textContent = `${this.currentProgress}%`;
  }

  private pollInitState(): void {
    if (!this.module) return;

    // Process WebGPU events — guard against concurrent calls which caused
    // Asyncify reentrancy crashes when emscripten_sleep(0) was in use.
    if (this.module._bespoke_process_events && !this.isProcessingEvents) {
      this.isProcessingEvents = true;
      try {
        this.module._bespoke_process_events();
      } finally {
        this.isProcessingEvents = false;
      }
    }

    // Get current init state from C++
    const state = this.module._bespoke_get_init_state?.() ?? 0;

    const webgpuStateToStep: Record<number, string> = {
      0: 'wasm_load',
      1: 'webgpu_instance',
      2: 'webgpu_adapter',
      3: 'renderer_pipelines',
      4: 'audio_init',
      5: 'controls_create',
    };

    const webglStateToStep: Record<number, string> = {
      0: 'wasm_load',
      6: 'webgl_context',
      7: 'webgl_capabilities',
      3: 'renderer_pipelines',
      4: 'audio_init',
      5: 'controls_create',
    };

    const stateToStep =
      this.rendererBackend === 'webgl' ? webglStateToStep : webgpuStateToStep;
    const currentMappedStep = stateToStep[state];
    const currentStepIndex = currentMappedStep
      ? this.initSteps.findIndex(s => s.id === currentMappedStep)
      : -1;

    if (currentStepIndex > 0) {
      for (let i = 0; i < currentStepIndex; i++) {
        const stepId = this.initSteps[i].id;
        if (!this.completedSteps.has(stepId)) {
          this.completeStep(stepId);
        }
      }
    }

    // Set active step for current state
    if (currentMappedStep && this.activeStep !== currentMappedStep) {
      this.setActiveStep(currentMappedStep);
    }
  }

  private async waitForAsyncInit(statePollInterval: number): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      console.log('Waiting for async initialization...');

      const initTimeout = window.setTimeout(() => {
        clearInterval(statePollInterval);
        delete bespokeWindow.__bespoke_on_init_complete;

        const elapsed = ((performance.now() - this.initStartTime) / 1000).toFixed(1);
        console.error(`Initialization timed out after ${elapsed}s`);

        reject(new Error(`Initialization timed out after ${elapsed}s. Check console for details.`));
      }, 60000); // 60 second timeout

      // statePollInterval (50ms) already calls _bespoke_process_events() via pollInitState().
      // A separate high-frequency poll interval is not needed and previously caused
      // Asyncify reentrancy crashes by calling _bespoke_process_events() concurrently.

      bespokeWindow.__bespoke_on_init_complete = (status: number) => {
        console.log('__bespoke_on_init_complete called with status:', status);

        window.clearTimeout(initTimeout);
        clearInterval(statePollInterval);
        delete bespokeWindow.__bespoke_on_init_complete;

        if (status === 0) {
          this.completeAllSteps();
          resolve();
        } else {
          const errorMsg = this.getInitErrorMessage(status);
          reject(new Error(`Initialization failed: ${errorMsg} (code: ${status})`));
        }
      };
    }).then(() => {
      // Initialization completed successfully
      this.isInitialized = true;
      publishRendererBreadcrumbs(
        this.module?._bespoke_get_renderer_backend?.() === 1 ? 'webgl' : 'webgpu',
        this.rendererFallbackReason,
      );
      this.showReadyState();
      this.setupEventListeners();
      this.startRenderLoop();
      this.applyPostInitOptions();
      
      const elapsed = ((performance.now() - this.initStartTime) / 1000).toFixed(2);
      console.log(`BespokeSynth initialized successfully in ${elapsed}s`);
    });
  }

  private getInitErrorMessage(code: number): string {
    const cppError = this.module?._bespoke_get_init_error?.();
    if (typeof cppError === 'number' && this.module?.UTF8ToString) {
      const detail = this.module.UTF8ToString(cppError);
      if (detail) return detail;
    }

    const messages: Record<number, string> = {
      [-1]: 'WebGPU initialization failed - browser may not support WebGPU',
      [-2]: 'Renderer initialization failed - shader compilation error',
      [-3]: 'Audio backend initialization failed',
      [-4]: 'Failed to start async WebGPU initialization',
      [-5]: 'WebGL2 initialization failed - see console for details',
    };
    return messages[code] || 'Unknown error';
  }

  private showReadyState(): void {
    const statusEl = document.getElementById('status');
    const subheaderEl = statusEl?.querySelector('.status-subheader');
    
    if (subheaderEl) {
      subheaderEl.textContent = 'Ready!';
    }
    
    // Hide status after a short delay
    setTimeout(() => {
      if (statusEl) {
        statusEl.classList.add('hidden');
      }
    }, 500);
  }

  private showErrorState(message: string): void {
    const statusEl = document.getElementById('status');
    const headerEl = statusEl?.querySelector('.status-header');
    const subheaderEl = statusEl?.querySelector('.status-subheader');
    
    if (statusEl) statusEl.classList.add('error');
    if (headerEl) headerEl.textContent = 'Initialization Failed';
    if (subheaderEl) subheaderEl.textContent = message;
    
    // Show error in progress bar
    const fillEl = document.getElementById('progress-fill');
    if (fillEl) {
      fillEl.style.width = '100%';
      fillEl.style.background = 'linear-gradient(90deg, #f44336, #ff5722)';
    }
    
    const textEl = document.getElementById('progress-text');
    if (textEl) {
      textEl.textContent = 'Error';
      textEl.style.color = '#f44336';
    }
    
    // Mark active step as error
    if (this.activeStep) {
      const stepEl = document.getElementById(`step-${this.activeStep}`);
      if (stepEl) {
        stepEl.classList.remove('active');
        stepEl.classList.add('error');
        stepEl.querySelector('.init-step-icon')!.textContent = '✗';
      }
    }
  }

  private resizeCanvas(): void {
    if (!this.canvas) return;

    const container = this.canvas.parentElement;
    if (container) {
      this.canvas.width = container.clientWidth;
      this.canvas.height = container.clientHeight;

      if (this.isInitialized && this.module?._bespoke_resize) {
        this.module._bespoke_resize(this.canvas.width, this.canvas.height);
      }
    }
  }

  private setupEventListeners(): void {
    if (!this.canvas || !this.module) return;
    const module = this.module;

    // Mouse events
    this.canvas.addEventListener('mousedown', (e) => {
      if (module._bespoke_mouse_down) {
        module._bespoke_mouse_down(e.offsetX, e.offsetY, e.button);
      }
    });

    this.canvas.addEventListener('contextmenu', (e) => e.preventDefault());

    this.canvas.addEventListener('mouseup', (e) => {
      if (module._bespoke_mouse_up) {
        module._bespoke_mouse_up(e.offsetX, e.offsetY, e.button);
      }
    });

    this.canvas.addEventListener('mousemove', (e) => {
      if (module._bespoke_mouse_move) {
        module._bespoke_mouse_move(e.offsetX, e.offsetY);
      }
    });

    this.canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      if (module._bespoke_mouse_wheel) {
        module._bespoke_mouse_wheel(e.deltaX, e.deltaY);
      }
    }, { passive: false });

    // Keyboard events
    document.addEventListener('keydown', (e) => {
      if (module._bespoke_key_down) {
        const modifiers = this.getModifiers(e);
        const keyCode = e.key.length === 1 ? e.key.toUpperCase().charCodeAt(0) : (e.keyCode || e.which);
        if (e.key === '/' || (e.ctrlKey && e.key.toLowerCase() === 'k')) e.preventDefault();
        module._bespoke_key_down(keyCode, modifiers);
      }
    });

    document.addEventListener('keyup', (e) => {
      if (module._bespoke_key_up) {
        const modifiers = this.getModifiers(e);
        const keyCode = e.code ? e.code.charCodeAt(0) : (e.keyCode || e.which);
        module._bespoke_key_up(keyCode, modifiers);
      }
    });

    // Control buttons
    const playBtn = document.getElementById('playBtn');
    const stopBtn = document.getElementById('stopBtn');

    if (playBtn) {
      playBtn.addEventListener('click', () => {
        if (module._bespoke_play) {
          module._bespoke_play();
        }
      });
    }

    if (stopBtn) {
      stopBtn.addEventListener('click', () => {
        if (module._bespoke_stop) {
          module._bespoke_stop();
        }
      });
    }
  }

  private getModifiers(e: KeyboardEvent): number {
    let modifiers = 0;
    if (e.shiftKey) modifiers |= 1;
    if (e.altKey) modifiers |= 2;
    if (e.ctrlKey) modifiers |= 4;
    if (e.metaKey) modifiers |= 8;
    return modifiers;
  }

  private startRenderLoop(): void {
    if (this.animationFrameId !== null) return;

    // Grab the overlay element once so we don't re-query every frame.
    this.guiOverlay = document.getElementById('gui-overlay');

    const renderFrame = () => {
      if (this.module?._bespoke_render) {
        this.module._bespoke_render();
      }
      // Sync hybrid text labels after every GPU frame
      this.updateControlOverlays();
      this.animationFrameId = requestAnimationFrame(renderFrame);
    };

    this.animationFrameId = requestAnimationFrame(renderFrame);
  }

  /**
   * Synchronise the DOM text overlay with the current knob positions and
   * values reported by the C inspection API.  Each knob gets one absolutely-
   * positioned `.gui-label` element (created lazily, reused thereafter).
   *
   * The overlay is only active in demo-panels view (view mode 1) where the
   * gControlInfoCache is populated; in modular-canvas mode there are no entries
   * and any existing labels are hidden.
   */
  private updateControlOverlays(): void {
    if (!this.guiOverlay || !this.module || !this.canvas) return;

    const count: number = this.module._bespoke_get_control_count?.() ?? 0;

    // Hide stale labels that exceed the current count
    this.labelElements.forEach((el, idx) => {
      if (idx >= count) el.style.display = 'none';
    });

    if (count === 0) return;

    // The canvas element may have CSS dimensions different from its pixel
    // dimensions (device-pixel-ratio scaling).  Compute the scale factors so
    // we can convert WASM pixel coordinates → CSS pixels.
    const cssW = this.canvas.clientWidth || this.canvas.width;
    const cssH = this.canvas.clientHeight || this.canvas.height;
    const scaleX = cssW / (this.canvas.width || cssW);
    const scaleY = cssH / (this.canvas.height || cssH);

    for (let i = 0; i < count; i++) {
      const ptr: number = this.module._bespoke_get_control_info?.(i) ?? 0;
      if (!ptr) continue;

      let info: {
        id: number; type: string; label: string; value: number;
        min: number; max: number; unit: string;
        x: number; y: number; size: number;
      };

      try {
        info = JSON.parse(this.module.UTF8ToString(ptr));
      } catch (e) {
        console.warn(`[BespokeSynth] Failed to parse control info for index ${i}:`, e);
        continue;
      }

      // Screen position: WASM reports top-left corner; we center the label
      // horizontally and place it below the knob.
      const cssCenterX = (info.x + info.size * 0.5) * scaleX;
      const cssBelowY  = (info.y + info.size + 4) * scaleY;

      // Format the display value (use the same units as getDisplayString)
      let displayVal = '';
      if (info.unit === 'Hz') {
        const v = info.value;
        displayVal = v >= 1000 ? `${(v / 1000).toFixed(2)} kHz` : `${Math.round(v)} Hz`;
      } else if (info.unit === '%') {
        displayVal = `${Math.round(info.value * 100)}%`;
      } else {
        displayVal = info.value.toFixed(2) + (info.unit ? ` ${info.unit}` : '');
      }

      // Create or recycle the label element
      let el = this.labelElements.get(i);
      if (!el) {
        el = document.createElement('div');
        el.className = 'gui-label';
        el.innerHTML =
          '<span class="label-name"></span>' +
          '<span class="label-value"></span>';
        this.guiOverlay.appendChild(el);
        this.labelElements.set(i, el);
      }

      el.style.display  = '';
      el.style.left     = `${cssCenterX}px`;
      el.style.top      = `${cssBelowY}px`;

      const nameEl  = el.querySelector<HTMLElement>('.label-name');
      const valueEl = el.querySelector<HTMLElement>('.label-value');
      if (nameEl)  nameEl.textContent  = info.label;
      if (valueEl) valueEl.textContent = displayVal;
    }
  }

  private stopRenderLoop(): void {
    if (this.animationFrameId !== null) {
      cancelAnimationFrame(this.animationFrameId);
      this.animationFrameId = null;
    }
  }

  shutdown(): void {
    this.stopRenderLoop();
    if (this.module?._bespoke_shutdown) {
      this.module._bespoke_shutdown();
    }
    this.isInitialized = false;
  }
}

// Start the application when DOM is ready
document.addEventListener('DOMContentLoaded', async () => {
  const app = new BespokeSynthApp();
  
  try {
    await app.init();
  } catch (error) {
    console.error('Application initialization failed:', error);
  }

  // Expose module creation API on window for console access
  (window as unknown as Record<string, unknown>).__bespoke = {
    createModule: (type: string, x: number, y: number) => {
      const mod = app.getModule();
      if (mod && mod.ccall) {
        return mod.ccall('bespoke_create_module', 'number', ['string', 'number', 'number'], [type, x, y]);
      }
      return -1;
    },
    deleteModule: (id: number) => {
      const mod = app.getModule();
      if (mod && mod._bespoke_delete_module) {
        mod._bespoke_delete_module(id);
      }
    },
    connectModules: (srcId: number, destId: number) => {
      const mod = app.getModule();
      if (mod && mod._bespoke_connect_modules) {
        mod._bespoke_connect_modules(srcId, destId);
      }
    },
    setViewMode: (mode: number) => {
      const mod = app.getModule();
      if (mod && mod._bespoke_set_view_mode) {
        mod._bespoke_set_view_mode(mode);
      }
    },
    getModuleCount: () => {
      const mod = app.getModule();
      if (mod && mod._bespoke_get_module_count) {
        return mod._bespoke_get_module_count();
      }
      return 0;
    },
    // Control inspection API
    getControlCount: () => {
      const mod = app.getModule();
      return mod?._bespoke_get_control_count?.() ?? 0;
    },
    getControlInfo: (index: number) => {
      const mod = app.getModule();
      if (!mod?._bespoke_get_control_info) return null;
      const ptr: number = mod._bespoke_get_control_info(index);
      if (!ptr) return null;
      try { return JSON.parse(mod.UTF8ToString(ptr)); } catch { return null; }
    },
    // Runtime theming API  (colorId matches ThemeColorId enum in Theme.h)
    setThemeColor: (colorId: number, r: number, g: number, b: number, a = 1.0) => {
      const mod = app.getModule();
      mod?._bespoke_set_theme_color?.(colorId, r, g, b, a);
    },
    resetTheme: () => {
      const mod = app.getModule();
      mod?._bespoke_reset_theme?.();
    },
    captureScreenshot: async (options?: CaptureScreenshotOptions) => app.captureScreenshot(options ?? {}),
    downloadScreenshot,
    onScreenshotCaptured,
    getStateJson: () => getPatchStateJson(app.getModule()),
    loadStateJson: (json: string) => loadPatchStateJson(app.getModule(), json),
    downloadPatch: (filename?: string) => downloadPatchState(app.getModule(), filename),
    loadPatchFile: () => promptForPatchStateFile(app.getModule()),
    loadBundledLayout: (path: string) => loadBundledLayout(app.getModule(), path),
    setRendererBackend: (backend: RendererBackend) => switchRendererPreference(backend),
    getRendererBackend: () => {
      const mod = app.getModule();
      if (mod?._bespoke_get_renderer_backend) {
        return mod._bespoke_get_renderer_backend() === 1 ? 'webgl' : 'webgpu';
      }
      return resolveRendererBackend();
    },
    setRenderTestMode: (enabled: boolean) => {
      const mod = app.getModule();
      mod?._bespoke_set_render_test_mode?.(enabled ? 1 : 0);
    },
    getRenderTestMode: () => {
      const mod = app.getModule();
      return mod?._bespoke_get_render_test_mode?.() === 1;
    },
    setWebGLDebugMode: (mode: number) => {
      const mod = app.getModule();
      mod?._bespoke_set_webgl_debug_mode?.(mode);
    },
    setFontTestVisible: (visible: boolean) => {
      const mod = app.getModule();
      mod?._bespoke_set_font_test_visible?.(visible ? 1 : 0);
      mod?._bespoke_render?.();
    },
    renderFrame: () => {
      app.getModule()?._bespoke_render?.();
    },
  };

  // Cleanup on page unload
  window.addEventListener('beforeunload', () => {
    app.shutdown();
  });
});

// Export for potential use as a library
export { BespokeSynthApp };
