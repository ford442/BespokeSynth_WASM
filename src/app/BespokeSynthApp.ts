import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import {
  publishRendererBreadcrumbs,
  captureCanvasScreenshot,
  downloadScreenshot,
  isRenderTestRequested,
  resolveRendererBackend,
  type CaptureScreenshotOptions,
  type RendererBackend,
} from '../rendererMode';
import {
  downloadPatchState,
  getPatchStateJson,
  loadBundledLayout,
  loadPatchStateJson,
  promptForPatchStateFile,
} from '../patchState';
import { PatchStorage, PATCH_DEFAULT_AUTOSAVE_NAME } from '../patchStorage';
import {
  resolveAudioBackend,
  isAudioWorkletPocRequested,
  isDebugRequested,
  type AudioBackendId,
} from '../audio/audioMode';
import { runAudioWorkletPoc } from '../audio/workletPoc';
import {
  canUseWorkletRingBackend,
  createWorkletRingBackend,
  type WorkletRingBackend,
} from '../audio/workletRingBackend';
import { bespokeWindow } from './browserWindow';
import { loadWasmModule } from './wasmLoader';
import {
  InitProgressController,
  WEBGL2_INIT_STEPS,
  WEBGPU_INIT_STEPS,
  showErrorState,
  showReadyState,
} from './initProgress';
import { installInputBridge } from './inputBridge';
import { startRenderLoop, syncControlOverlays, type RenderLoopHandle } from './renderLoop';
import {
  createPatchStatusHelpers,
  setupPatchPanel,
} from '../ui/patchPanel';
import {
  setupFloatingRendererToggle,
  setupKeyboardShortcuts,
  setupRendererPanel,
} from '../ui/rendererPanel';
import { setupAudioHealthHud } from '../ui/audioHealthHud';
import { collectSampleHashes } from '../samples/opfsStore';
import {
  installSampleDropTarget,
  promptForAudioFile,
  restorePatchSamples,
} from '../samples/sampleIo';
import { decodePatchFragment, writeShareUrl } from '../samples/patchShare';
import { downloadOfflineWav } from '../samples/offlineExport';
import { startInputCapture, type InputCaptureHandle } from '../audio/inputCapture';

export class BespokeSynthApp {
  private canvas: HTMLCanvasElement | null = null;
  private module: BespokeSynthModule | null = null;
  private renderLoop: RenderLoopHandle | null = null;
  private isInitialized = false;
  private isProcessingEvents = false;
  private readonly patchStorage = new PatchStorage();
  private selectedPatchId: string | undefined;
  private autoSaveTimer: number | null = null;
  private lastAutoSaveJson = '';
  private autoSaveDefaultName = PATCH_DEFAULT_AUTOSAVE_NAME;
  private autoSaveNamePrompted = false;
  private refreshPatchList: (() => Promise<unknown>) | null = null;
  private readonly initProgress = new InitProgressController();
  private guiOverlay: HTMLElement | null = null;
  private readonly labelElements = new Map<number, HTMLElement>();
  private rendererBackend: RendererBackend = resolveRendererBackend();
  private rendererFallbackReason: string | null = null;
  private audioBackendId: AudioBackendId = resolveAudioBackend();
  private workletRing: WorkletRingBackend | null = null;
  private readonly patchStatusHelpers = createPatchStatusHelpers();
  private inputCapture: InputCaptureHandle | null = null;

  getModule(): BespokeSynthModule | null {
    return this.module;
  }

  getHostFrameCount(): number {
    return this.renderLoop?.getFrameCount() ?? 0;
  }

  async play(): Promise<void> {
    await this.startPlayback();
  }

  stop(): void {
    this.stopPlayback();
  }

  async init(): Promise<void> {
    this.initProgress.markInitStart();
    console.log('Initializing BespokeSynth WASM...');

    this.canvas = document.getElementById('canvas') as HTMLCanvasElement;
    if (!this.canvas) {
      throw new Error('Canvas element not found');
    }

    this.resizeCanvas();
    window.addEventListener('resize', () => this.resizeCanvas());

    this.initProgress.setInitSteps(
      this.rendererBackend === 'webgl' ? WEBGL2_INIT_STEPS : WEBGPU_INIT_STEPS,
    );
    this.initProgress.initializeProgressUI();
    this.initProgress.setupInitProgressCallback();

    setupRendererPanel({
      rendererBackend: this.rendererBackend,
      audioBackendId: this.audioBackendId,
      getModule: () => this.module,
      onScreenshot: () => void this.captureScreenshot(),
      onSavePatch: () => {
        if (this.module) downloadPatchState(this.module);
      },
      onLoadPatch: () => {
        if (this.module) void promptForPatchStateFile(this.module);
      },
      onImportSample: () => {
        if (!this.module) return;
        void promptForAudioFile(this.module).then((id) => {
          this.patchStatusHelpers.showPatchStatus(id >= 0 ? 'Sample loaded' : 'Sample import failed');
        });
      },
      onExportWav: () => {
        if (!this.module) return;
        const ok = downloadOfflineWav(this.module, { seconds: 4, bitsPerSample: 32 });
        this.patchStatusHelpers.showPatchStatus(ok ? 'Exported WAV' : 'Export failed');
      },
      onSharePatch: () => {
        void this.shareCurrentPatch();
      },
      onToggleInput: () => {
        void this.toggleInputCapture();
      },
    });

    const controls = document.querySelector('#header .controls');
    if (controls) {
      const patchBindings = setupPatchPanel(controls, () => this.module, this.patchStorage, {
        getSelectedPatchId: () => this.selectedPatchId,
        setSelectedPatchId: (id) => {
          this.selectedPatchId = id;
        },
        getLastAutoSaveJson: () => this.lastAutoSaveJson,
        setLastAutoSaveJson: (json) => {
          this.lastAutoSaveJson = json;
        },
        configureAutoSave: (enabled) => this.configureAutoSave(enabled),
        showPatchStatus: (message, durationMs) => this.patchStatusHelpers.showPatchStatus(message, durationMs),
      });
      this.refreshPatchList = patchBindings.refreshPatchList;
    }

    setupKeyboardShortcuts(this.rendererBackend, () => void this.captureScreenshot());

    if (isDebugRequested()) {
      setupFloatingRendererToggle(this.rendererBackend);
      setupAudioHealthHud(() => this.module);
    }

    const statusSubheader = document.querySelector('#status .status-subheader');
    if (statusSubheader) {
      statusSubheader.textContent =
        this.rendererBackend === 'webgl'
          ? 'Setting up WebGL2 and audio...'
          : 'Setting up WebGPU and audio...';
    }

    try {
      this.initProgress.setActiveStep('wasm_load');
      this.module = await loadWasmModule(this.canvas ?? undefined);
      this.initProgress.completeStep('wasm_load');
      console.log('WASM module loaded successfully');

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

      const sampleRate = 44100;
      const bufferSize = 512;
      const statePollInterval = window.setInterval(() => {
        this.pollInitState();
      }, 50);

      const result = this.module._bespoke_init?.(
        this.canvas.width,
        this.canvas.height,
        sampleRate,
        bufferSize,
      );

      if (result === 0) {
        clearInterval(statePollInterval);
        this.completeInitialization();
        console.log('BespokeSynth initialized successfully (sync)');
      } else if (result === 1) {
        try {
          await this.waitForAsyncInit(statePollInterval);
        } catch (asyncError) {
          const canFallback =
            this.rendererBackend === 'webgpu' &&
            !this.wasWebGPUExplicitlyRequested() &&
            this.module?._bespoke_shutdown;

          if (canFallback && (await this.retryWithWebGL2(sampleRate, bufferSize, statePollInterval))) {
            clearInterval(statePollInterval);
            this.completeInitialization();
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
      showErrorState(error instanceof Error ? error.message : 'Unknown error', {
        canRetryWebGL: this.rendererBackend === 'webgpu',
      });
    }
  }

  private completeInitialization(): void {
    this.initProgress.completeAllSteps();
    this.isInitialized = true;
    publishRendererBreadcrumbs(
      this.module?._bespoke_get_renderer_backend?.() === 1 ? 'webgl' : 'webgpu',
      this.rendererFallbackReason,
    );
    showReadyState(this.rendererFallbackReason);
    this.startHostLoop();
    this.applyPostInitOptions();
  }

  private startHostLoop(): void {
    if (!this.canvas || !this.module || this.renderLoop) return;

    this.guiOverlay = document.getElementById('gui-overlay');
    installInputBridge(this.canvas, this.module, {
      onPlay: () => void this.startPlayback(),
      onStop: () => this.stopPlayback(),
    });

    this.renderLoop = startRenderLoop({
      module: this.module,
      canvas: this.canvas,
      syncOverlay: (module, canvas) => {
        syncControlOverlays(module, canvas, this.guiOverlay, this.labelElements);
      },
    });

    installSampleDropTarget(this.canvas, () => this.module, (message) => {
      this.patchStatusHelpers.showPatchStatus(message);
    });
  }

  private replaceCanvasElement(): void {
    if (!this.canvas?.parentElement) return;

    const parent = this.canvas.parentElement;
    const replacement = document.createElement('canvas');
    replacement.id = 'canvas';
    replacement.width = this.canvas.width;
    replacement.height = this.canvas.height;
    parent.replaceChild(replacement, this.canvas);
    this.canvas = replacement;

    const moduleWithCanvas = this.module as (BespokeSynthModule & { canvas?: HTMLCanvasElement }) | null;
    if (moduleWithCanvas) {
      moduleWithCanvas.canvas = replacement;
    }
  }

  private wasWebGPUExplicitlyRequested(): boolean {
    const params = new URLSearchParams(window.location.search);
    const explicit = params.get('renderer')?.toLowerCase();
    return explicit === 'webgpu' || params.has('webgpu');
  }

  private async retryWithWebGL2(
    sampleRate: number,
    bufferSize: number,
    previousPollInterval?: number,
  ): Promise<boolean> {
    if (!this.canvas || !this.module) return false;

    console.warn('WebGPU init failed; retrying with WebGL2 fallback...');
    if (previousPollInterval !== undefined) {
      clearInterval(previousPollInterval);
    }
    delete bespokeWindow.__bespoke_on_init_complete;

    this.module._bespoke_shutdown?.();
    this.replaceCanvasElement();
    this.rendererBackend = 'webgl';
    this.rendererFallbackReason = 'WebGPU initialization failed; fell back to WebGL2';
    this.initProgress.setInitSteps(WEBGL2_INIT_STEPS);
    this.initProgress.initializeProgressUI();
    this.module._bespoke_set_renderer_backend?.(1);
    publishRendererBreadcrumbs(this.rendererBackend, this.rendererFallbackReason);

    const debugSelect = document.getElementById('webglDebugSelect') as HTMLSelectElement | null;
    if (debugSelect) debugSelect.disabled = false;

    const statusSubheader = document.querySelector('#status .status-subheader');
    if (statusSubheader) {
      statusSubheader.textContent = 'Retrying with WebGL2 renderer...';
    }
    const statusEl = document.getElementById('status');
    statusEl?.classList.remove('error');
    statusEl?.querySelector('.init-recovery')?.remove();

    const statePollInterval = window.setInterval(() => {
      this.pollInitState();
    }, 50);

    try {
      const result = this.module._bespoke_init?.(
        this.canvas.width,
        this.canvas.height,
        sampleRate,
        bufferSize,
      );

      if (result === 0) {
        this.initProgress.completeAllSteps();
        return true;
      }
      if (result === 1) {
        await this.awaitAsyncInitCompletion(statePollInterval);
        return true;
      }
      return false;
    } catch (error) {
      console.error('WebGL2 fallback initialization failed:', error);
      return false;
    } finally {
      clearInterval(statePollInterval);
      delete bespokeWindow.__bespoke_on_init_complete;
    }
  }

  private configureAutoSave(enabled: boolean): void {
    if (this.autoSaveTimer !== null) window.clearInterval(this.autoSaveTimer);
    this.autoSaveTimer = enabled ? window.setInterval(() => void this.runAutoSave(), 1500) : null;
  }

  private async runAutoSave(): Promise<void> {
    if (!this.module) return;
    const json = getPatchStateJson(this.module);
    if (json === this.lastAutoSaveJson) return;
    this.lastAutoSaveJson = json;

    let name = this.autoSaveDefaultName;
    if (!this.selectedPatchId && !this.autoSaveNamePrompted) {
      this.autoSaveNamePrompted = true;
      const prompted = window.prompt('Name your auto-save', this.autoSaveDefaultName);
      if (prompted !== null && prompted.trim()) {
        name = prompted.trim();
        this.autoSaveDefaultName = name;
      }
    }

    try {
      const patch = await this.patchStorage.save(name, json, this.selectedPatchId);
      this.selectedPatchId = patch.id;
      await this.refreshPatchList?.();
      this.patchStatusHelpers.showPatchStatus(`Auto-saved "${patch.name}"`);
    } catch (error) {
      console.error('Auto-save failed:', error);
      this.patchStatusHelpers.showPatchStatus('Auto-save failed');
    }
  }

  private async startPlayback(): Promise<void> {
    if (!this.module) return;

    if (this.audioBackendId === 'worklet') {
      const gate = canUseWorkletRingBackend();
      if (!gate.ok) {
        console.warn('Worklet audio unavailable, falling back to SDL2:', gate.reason);
        this.module._bespoke_set_external_audio?.(0);
        this.module._bespoke_set_audio_backend_id?.(0);
        this.module._bespoke_play?.();
        return;
      }
      if (!this.workletRing) {
        this.workletRing = createWorkletRingBackend(this.module);
      }
      if (!this.workletRing.isRunning()) {
        await this.workletRing.start();
      }
      this.module._bespoke_play?.();
      return;
    }

    this.module._bespoke_set_external_audio?.(0);
    this.module._bespoke_set_audio_backend_id?.(0);
    this.module._bespoke_play?.();
  }

  private stopPlayback(): void {
    if (!this.module) return;
    this.module._bespoke_stop?.();
    this.workletRing?.stop();
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

    this.module._bespoke_set_audio_backend_id?.(this.audioBackendId === 'worklet' ? 1 : 0);
    if (this.audioBackendId === 'worklet') {
      const gate = canUseWorkletRingBackend();
      console.log(
        gate.ok
          ? 'Audio backend: worklet (SAB ring) — starts on Play'
          : `Audio backend: worklet requested but unavailable (${gate.reason}); Play will fall back to SDL2`,
      );
    } else {
      console.log('Audio backend: SDL2 (default)');
    }

    if (isAudioWorkletPocRequested()) {
      void runAudioWorkletPoc().then((result) => {
        console.log('AudioWorklet POC result:', result);
        (window as unknown as Record<string, unknown>).__bespokeAudioWorkletPoc = result;
        this.module?._bespoke_set_audio_backend_id?.(2);
      });
    }

    void this.loadSharedPatchFromUrl();
  }

  private async loadSharedPatchFromUrl(): Promise<void> {
    if (!this.module) return;
    const json = await decodePatchFragment(window.location.hash);
    if (!json) return;
    await restorePatchSamples(this.module, collectSampleHashes(json));
    if (loadPatchStateJson(this.module, json)) {
      this.patchStatusHelpers.showPatchStatus('Loaded shared patch');
    }
  }

  private async shareCurrentPatch(): Promise<void> {
    if (!this.module) return;
    const json = getPatchStateJson(this.module);
    const url = await writeShareUrl(json);
    try {
      await navigator.clipboard.writeText(url);
      this.patchStatusHelpers.showPatchStatus('Share URL copied');
    } catch {
      this.patchStatusHelpers.showPatchStatus('Share URL updated');
    }
  }

  private async toggleInputCapture(): Promise<void> {
    if (!this.module) return;
    if (this.inputCapture?.running()) {
      this.inputCapture.stop();
      this.inputCapture = null;
      this.patchStatusHelpers.showPatchStatus('Mic input off');
      return;
    }
    try {
      this.inputCapture = await startInputCapture(this.module);
      this.patchStatusHelpers.showPatchStatus('Mic input on');
    } catch (error) {
      console.error('Mic capture failed', error);
      this.patchStatusHelpers.showPatchStatus('Mic permission denied');
    }
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

  private pollInitState(): void {
    if (!this.module) return;

    if (this.module._bespoke_process_events && !this.isProcessingEvents) {
      this.isProcessingEvents = true;
      try {
        this.module._bespoke_process_events();
      } finally {
        this.isProcessingEvents = false;
      }
    }

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
      ? this.initProgress.getInitSteps().findIndex((s) => s.id === currentMappedStep)
      : -1;

    if (currentStepIndex > 0) {
      for (let i = 0; i < currentStepIndex; i++) {
        const stepId = this.initProgress.getInitSteps()[i].id;
        if (!this.initProgress.getCompletedSteps().has(stepId)) {
          this.initProgress.completeStep(stepId);
        }
      }
    }

    if (currentMappedStep && this.initProgress.getActiveStep() !== currentMappedStep) {
      this.initProgress.setActiveStep(currentMappedStep);
    }
  }

  private awaitAsyncInitCompletion(statePollInterval: number): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      console.log('Waiting for async initialization...');

      const initTimeout = window.setTimeout(() => {
        clearInterval(statePollInterval);
        delete bespokeWindow.__bespoke_on_init_complete;

        const elapsed = ((performance.now() - this.initProgress.getInitStartTime()) / 1000).toFixed(1);
        console.error(`Initialization timed out after ${elapsed}s`);

        reject(new Error(`Initialization timed out after ${elapsed}s. Check console for details.`));
      }, 60000);

      bespokeWindow.__bespoke_on_init_complete = (status: number) => {
        console.log('__bespoke_on_init_complete called with status:', status);

        window.clearTimeout(initTimeout);
        clearInterval(statePollInterval);
        delete bespokeWindow.__bespoke_on_init_complete;

        if (status === 0) {
          this.initProgress.completeAllSteps();
          resolve();
        } else {
          const errorMsg = this.getInitErrorMessage(status);
          reject(new Error(`Initialization failed: ${errorMsg} (code: ${status})`));
        }
      };
    });
  }

  private async waitForAsyncInit(statePollInterval: number): Promise<void> {
    return this.awaitAsyncInitCompletion(statePollInterval).then(() => {
      this.completeInitialization();
      const elapsed = ((performance.now() - this.initProgress.getInitStartTime()) / 1000).toFixed(2);
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

  shutdown(): void {
    this.inputCapture?.stop();
    this.inputCapture = null;
    this.renderLoop?.stop();
    this.renderLoop = null;
    if (this.module?._bespoke_shutdown) {
      this.module._bespoke_shutdown();
    }
    this.isInitialized = false;
  }
}
