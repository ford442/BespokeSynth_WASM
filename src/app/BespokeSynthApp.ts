import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import {
  publishRendererBreadcrumbs,
  captureCanvasScreenshot,
  downloadScreenshot,
  getRenderTestScene,
  isRenderTestRequested,
  isLegacyPanelsRequested,
  isPixelFontRequested,
  resolveRendererBackend,
  type CaptureScreenshotOptions,
  type RendererBackend,
} from '../rendererMode';
import {
  downloadPatchState,
  getPatchStateJson,
  loadBundledLayout,
  loadPatchStateJson,
  placeholderNotice,
  promptForPatchStateFile,
} from '../patchState';
import { PatchStorage, PATCH_DEFAULT_AUTOSAVE_NAME } from '../patchStorage';
import {
  resolveAudioBackendChoice,
  persistUrlAudioPreference,
  isAudioWorkletPocRequested,
  isDebugRequested,
  type AudioBackendChoice,
  type AudioBackendId,
} from '../audio/audioMode';
import {
  canUseWorkletRingBackend,
  createWorkletRingBackend,
  type WorkletRingBackend,
  type WorkletRingStats,
} from '../audio/workletRingBackend';
import {
  AUDIO_BACKEND_WORKLET_WASM,
  compileWorkletEngine,
  createWorkletEngineBackend,
  type WorkletEngineBackend,
} from '../audio/workletEngineBackend';
import { readAudioHealth, type AudioHealth } from '../audio/audioHealth';
import { bespokeWindow } from './browserWindow';
import { withCStrings } from './wasmStrings';
import { loadWasmModule } from './wasmLoader';
import {
  InitProgressController,
  WEBGL2_INIT_STEPS,
  WEBGPU_INIT_STEPS,
  showErrorState,
  showReadyState,
  updateFallbackBadge,
} from './initProgress';
import { installCanvasInput, installInputBridge } from './inputBridge';
import { startRenderLoop, type RenderLoopHandle } from './renderLoop';
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
import { setupDemoGallery } from '../ui/demoGallery';
import { collectSampleHashes } from '../samples/opfsStore';
import {
  installSampleDropTarget,
  promptForAudioFile,
  restorePatchSamples,
} from '../samples/sampleIo';
import { decodePatchFragment, writeShareUrl } from '../samples/patchShare';
import { downloadOfflineWav } from '../samples/offlineExport';
import { startInputCapture, type InputCaptureHandle } from '../audio/inputCapture';
import { LiveInstrument } from './liveInstrument';
import { setupScriptPanel, type ScriptPanelHandle } from '../ui/scriptPanel';

/** `?patch=<name>` loads one of the patches shipped in the resource pack. */
const BUNDLED_PATCHES: Record<string, string> = {
  starter: 'savestate/wasm-starter.bsk',
  drumkit: 'savestate/wasm-drumkit.bsk',
};

export class BespokeSynthApp {
  private canvas: HTMLCanvasElement | null = null;
  private module: BespokeSynthModule | null = null;
  private renderLoop: RenderLoopHandle | null = null;
  private isInitialized = false;
  // A context lost before init completes: bespoke_reinit_renderer needs a finished init.
  private pendingContextLoss: RendererBackend | null = null;
  private isProcessingEvents = false;
  private readonly patchStorage = new PatchStorage();
  private selectedPatchId: string | undefined;
  private autoSaveTimer: number | null = null;
  private lastAutoSaveJson = '';
  private autoSaveDefaultName = PATCH_DEFAULT_AUTOSAVE_NAME;
  private autoSaveNamePrompted = false;
  private refreshPatchList: (() => Promise<unknown>) | null = null;
  private readonly initProgress = new InitProgressController();
  private rendererBackend: RendererBackend = resolveRendererBackend();
  private rendererFallbackReason: string | null = null;
  // Logical (CSS) canvas size in step with canvas.width/height, which is
  // sized in physical (devicePixelRatio-scaled) pixels for HiDPI sharpness.
  // The WASM side does all layout/hit-testing math in logical pixels.
  private cssWidth = 800;
  private cssHeight = 600;
  private readonly audioBackendChoice: AudioBackendChoice = resolveAudioBackendChoice();
  private audioBackendId: AudioBackendId = this.audioBackendChoice.backend;
  private workletRing: WorkletRingBackend | null = null;
  /** Option A: the graph runs in a DSP-only WASM instance inside the AudioWorklet. */
  private workletEngine: WorkletEngineBackend | null = null;
  private workletEngineStarting: Promise<boolean> | null = null;
  private workletEngineError: string | null = null;
  private readonly patchStatusHelpers = createPatchStatusHelpers();
  private inputCapture: InputCaptureHandle | null = null;
  /** In-flight Play: a second click while the backend starts joins it instead of opening another context. */
  private playbackStarting: Promise<void> | null = null;
  private playRequested = false;
  private recoveringRenderer = false;
  private readonly disposers: Array<() => void> = [];
  readonly live = new LiveInstrument(() => this.module);
  private scriptPanel: ScriptPanelHandle | null = null;

  getModule(): BespokeSynthModule | null {
    return this.module;
  }

  getScriptPanel(): ScriptPanelHandle | null {
    return this.scriptPanel;
  }

  getAudioBackendChoice(): AudioBackendChoice {
    return this.audioBackendChoice;
  }

  getAudioRingStats(): WorkletRingStats | null {
    return this.workletEngine?.getStats() ?? this.workletRing?.getStats() ?? null;
  }

  /**
   * The playing engine's health: the worklet engine's while it plays (heapBytes and
   * mainThreadBlocks still describe the UI module), else the UI module's own.
   */
  getAudioHealth(): AudioHealth | null {
    const main = readAudioHealth(this.module);
    const engine = this.workletEngine?.getHealth();
    if (!engine) {
      return main && this.workletEngineError ? { ...main, engineError: this.workletEngineError } : main;
    }
    return {
      ...engine,
      heapBytes: main?.heapBytes,
      mainThreadBlocks: main?.callbackCount ?? 0,
      noteMarginMs: this.live.noteClock.getMarginMs(),
    };
  }

  /** Mirrors the worklet engine's beat, meters and scope samples into the UI module now. */
  syncAudioEngine(): void {
    if (!this.workletEngine) return;
    this.workletEngine.syncToMain();
    this.live.noteClock.observeLateNotes(this.workletEngine.lateNoteCount());
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
    persistUrlAudioPreference();

    this.canvas = document.getElementById('canvas') as HTMLCanvasElement | null;
    if (!this.canvas) {
      throw new Error('Canvas element not found');
    }

    this.resizeCanvas();
    const onResize = () => this.resizeCanvas();
    window.addEventListener('resize', onResize);
    this.disposers.push(() => window.removeEventListener('resize', onResize));
    // The header wraps when controls overflow, which resizes the canvas area without a window resize.
    if (typeof ResizeObserver === 'function' && this.canvas.parentElement) {
      const observer = new ResizeObserver(onResize);
      observer.observe(this.canvas.parentElement);
      this.disposers.push(() => observer.disconnect());
    }
    this.watchDevicePixelRatio();

    this.initProgress.setInitSteps(
      this.rendererBackend === 'webgl' ? WEBGL2_INIT_STEPS : WEBGPU_INIT_STEPS,
    );
    this.initProgress.initializeProgressUI();
    this.initProgress.setupInitProgressCallback();

    setupRendererPanel({
      rendererBackend: this.rendererBackend,
      audioBackendChoice: this.audioBackendChoice,
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
          this.patchStatusHelpers.showPatchStatus(id >= 0 ? 'Imported' : 'Import failed');
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
      live: this.live,
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
      setupDemoGallery(controls, () => this.module, {
        showPatchStatus: (message, durationMs) => this.patchStatusHelpers.showPatchStatus(message, durationMs),
      });
      this.scriptPanel = setupScriptPanel(controls, () => this.module, this.live, () =>
        this.workletEngine?.isRunning() ? this.workletEngine : null,
      );
    }

    setupKeyboardShortcuts(this.rendererBackend, () => void this.captureScreenshot());

    if (isDebugRequested()) {
      setupFloatingRendererToggle(this.rendererBackend);
      setupAudioHealthHud(
        () => this.getAudioHealth(),
        () => this.getAudioRingStats(),
      );
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
      this.module = await loadWasmModule(this.canvas);
      this.initProgress.completeStep('wasm_load');
      console.log('WASM module loaded successfully');
      bespokeWindow.__bespoke_on_device_lost = () => this.handleDeviceLost();
      bespokeWindow.__bespoke_on_canvas_notice = (message) => this.patchStatusHelpers.showPatchStatus(message, 5000);
      bespokeWindow.__bespoke_on_samples_missing = () => void this.restoreMissingSamples();

      const backendCode = this.rendererBackend === 'webgl' ? 1 : 0;
      this.module._bespoke_set_renderer_backend(backendCode);
      // Persists across the WebGL2 retry: the font mode lives outside the renderer.
      this.module._bespoke_set_font_mode(isPixelFontRequested() ? 1 : 0);
      // Spawn-menu descriptions and hover tooltips follow the browser language (English fallback).
      const helpModule = this.module;
      withCStrings(helpModule, [navigator.language], (tag) => helpModule._bespoke_set_help_locale(tag));
      this.module._bespoke_set_webgl_preserve_drawing_buffer(
        this.shouldPreserveWebGLDrawingBuffer() ? 1 : 0,
      );
      if (isLegacyPanelsRequested()) {
        this.module._bespoke_set_legacy_panels(1);
        console.log('Legacy demo panels enabled (?legacyPanels=1)');
      }
      publishRendererBreadcrumbs(this.rendererBackend, this.rendererFallbackReason);

      const sampleRate = 44100;
      const bufferSize = 512;
      const statePollInterval = window.setInterval(() => {
        this.pollInitState();
      }, 50);

      try {
        let result = 1;
        try {
          result = this.module._bespoke_init(this.cssWidth, this.cssHeight, sampleRate, bufferSize);
          if (result !== 0 && result !== 1) {
            throw new Error(`Initialization failed: ${this.getInitErrorMessage(result)} (code: ${result})`);
          }
          if (result === 1) await this.waitForAsyncInit(statePollInterval);
        } catch (initError) {
          // Synchronous (-4: no surface or no navigator.gpu), thrown and asynchronous WebGPU
          // failures fall back alike.
          const canFallback = this.rendererBackend === 'webgpu' && !this.wasWebGPUExplicitlyRequested();
          if (canFallback && (await this.retryWithWebGL2(sampleRate, bufferSize, statePollInterval))) {
            this.completeInitialization();
            console.log('BespokeSynth initialized with WebGL2 fallback');
            return;
          }
          throw initError;
        }
        if (result === 0) {
          this.completeInitialization();
          console.log('BespokeSynth initialized successfully (sync)');
        }
      } finally {
        // Every exit, including an unexpected code: otherwise process_events runs every 50 ms forever.
        clearInterval(statePollInterval);
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
      this.module?._bespoke_get_renderer_backend() === 1 ? 'webgl' : 'webgpu',
      this.rendererFallbackReason,
    );
    showReadyState(this.rendererFallbackReason);
    this.startHostLoop();
    this.applyPostInitOptions();
    const lost = this.pendingContextLoss;
    this.pendingContextLoss = null;
    // Only if init still runs on the backend that was lost (a failed init already fell back).
    if (lost && this.module?._bespoke_get_renderer_backend() === (lost === 'webgl' ? 1 : 0)) {
      this.handleContextLost(lost);
    }
  }

  private startHostLoop(): void {
    if (!this.canvas || !this.module || this.renderLoop) return;

    installInputBridge(this.canvas, this.module, {
      onPlay: () => void this.startPlayback(),
      onStop: () => this.stopPlayback(),
      piano: this.live.piano,
    });

    this.startRenderLoopOnly();
    this.installCanvasExtras(this.canvas);
  }

  private startRenderLoopOnly(): void {
    if (!this.canvas || !this.module) return;
    this.renderLoop?.stop();
    this.renderLoop = startRenderLoop({
      module: this.module,
      canvas: this.canvas,
      beforeFrame: () => this.syncAudioEngine(),
      onError: (error) => {
        this.renderLoop = null;
        const detail = error instanceof Error ? error.message : String(error);
        this.showRecoverableError('Renderer Stopped', `The render loop hit an error (${detail}). Reload to continue.`);
      },
    });
  }

  /** Per-element hooks that a replacement canvas needs again (input lives on the element). */
  private installCanvasExtras(canvas: HTMLCanvasElement): void {
    installSampleDropTarget(canvas, () => this.module, (message) => {
      this.patchStatusHelpers.showPatchStatus(message);
    });
    // WebGL2 context loss (GPU reset, too many contexts): the app recovers on a fresh canvas.
    canvas.addEventListener('webglcontextlost', (event) => {
      event.preventDefault();
      if (canvas === this.canvas) this.handleContextLost('webgl');
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

  private shouldPreserveWebGLDrawingBuffer(): boolean {
    const params = new URLSearchParams(window.location.search);
    const explicit = params.get('renderer')?.toLowerCase();
    return explicit === 'webgl' || explicit === 'webgl2' || params.has('webgl') || isRenderTestRequested();
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

    this.module._bespoke_shutdown();
    this.replaceCanvasElement();
    this.rendererBackend = 'webgl';
    this.rendererFallbackReason = 'WebGPU initialization failed; fell back to WebGL2';
    this.initProgress.setInitSteps(WEBGL2_INIT_STEPS);
    this.initProgress.initializeProgressUI();
    this.module._bespoke_set_renderer_backend(1);
    this.module._bespoke_set_webgl_preserve_drawing_buffer(
      this.shouldPreserveWebGLDrawingBuffer() ? 1 : 0,
    );
    if (isLegacyPanelsRequested()) {
      this.module._bespoke_set_legacy_panels(1);
    }
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
      const result = this.module._bespoke_init(this.cssWidth, this.cssHeight, sampleRate, bufferSize);

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
    this.autoSaveTimer = null;
    if (!enabled) return;
    // Ask for a name here, in the click that enabled auto-save, never from the timer.
    if (!this.selectedPatchId && !this.autoSaveNamePrompted) {
      this.autoSaveNamePrompted = true;
      const prompted = window.prompt('Name your auto-save', this.autoSaveDefaultName);
      if (prompted !== null && prompted.trim()) this.autoSaveDefaultName = prompted.trim();
    }
    this.autoSaveTimer = window.setInterval(() => void this.runAutoSave(), 1500);
  }

  private async runAutoSave(): Promise<void> {
    if (!this.module) return;
    const json = getPatchStateJson(this.module);
    if (json === this.lastAutoSaveJson) return;
    this.lastAutoSaveJson = json;

    try {
      const patch = await this.patchStorage.save(this.autoSaveDefaultName, json, this.selectedPatchId);
      this.selectedPatchId = patch.id;
      await this.refreshPatchList?.();
      this.patchStatusHelpers.showPatchStatus(`Auto-saved "${patch.name}"`);
    } catch (error) {
      console.error('Auto-save failed:', error);
      this.patchStatusHelpers.showPatchStatus('Auto-save failed');
    }
  }

  /** Never rejects: every failure ends in SDL playback (or a status message). */
  private startPlayback(): Promise<void> {
    this.playRequested = true;
    if (!this.playbackStarting) {
      this.playbackStarting = this.startPlaybackOnce()
        .catch((error: unknown) => {
          console.error('Playback failed to start:', error);
          this.patchStatusHelpers.showPatchStatus('Audio failed to start');
        })
        .finally(() => {
          this.playbackStarting = null;
        });
    }
    return this.playbackStarting;
  }

  private async startPlaybackOnce(): Promise<void> {
    if (!this.module) return;

    if (this.audioBackendId === 'worklet' && (await this.ensureWorkletEngine())) {
      this.playIfStillRequested();
      return;
    }

    if (this.audioBackendId === 'worklet' || this.audioBackendId === 'ring') {
      const gate = canUseWorkletRingBackend();
      if (gate.ok) {
        this.workletRing ??= createWorkletRingBackend(this.module);
        try {
          await this.workletRing.start();
          this.playIfStillRequested();
          return;
        } catch (error) {
          // start() rolled itself back (external audio off, context closed); stay on SDL from now on.
          const reason = error instanceof Error ? error.message : String(error);
          this.workletRing.dispose();
          this.workletRing = null;
          this.audioBackendId = 'sdl';
          const message = `Audio: worklet unavailable (${reason}); using SDL2`;
          console.warn(message);
          this.patchStatusHelpers.showPatchStatus(message, 8000);
        }
      } else {
        console.warn('Worklet audio unavailable, falling back to SDL2:', gate.reason);
      }
    }

    this.module._bespoke_set_external_audio(0);
    this.module._bespoke_set_audio_backend_id(0);
    this.playIfStillRequested();
  }

  /** Stop pressed while the backend was starting wins. */
  private playIfStillRequested(): void {
    if (!this.playRequested) {
      this.workletRing?.stop();
      return;
    }
    this.module?._bespoke_play();
  }

  private stopPlayback(): void {
    if (!this.module) return;
    this.playRequested = false;
    this.module._bespoke_stop();
    this.workletRing?.stop();
  }

  /**
   * Starts the worklet engine once. On failure (no engine build, ABI mismatch, worklet error)
   * the app falls back to the main-thread SAB ring for the rest of the session.
   */
  private ensureWorkletEngine(): Promise<boolean> {
    if (this.workletEngine?.isRunning()) return Promise.resolve(true);
    if (!this.module || this.audioBackendId !== 'worklet') return Promise.resolve(false);
    if (!this.workletEngineStarting) {
      const engine = createWorkletEngineBackend(this.module);
      this.workletEngineStarting = engine.start().then(
        () => {
          this.workletEngine = engine;
          this.live.graphNotes.setSource(engine.graphNotes);
          console.log('Audio backend: worklet engine (graph runs in the AudioWorklet)');
          return true;
        },
        (error: unknown) => {
          engine.dispose();
          this.workletEngineError = error instanceof Error ? error.message : String(error);
          this.audioBackendId = 'ring';
          const message = `Audio: worklet engine unavailable (${this.workletEngineError}); using the main-thread ring`;
          console.warn(message);
          this.patchStatusHelpers.showPatchStatus(message, 8000);
          return false;
        },
      );
      void this.workletEngineStarting.finally(() => {
        this.workletEngineStarting = null;
      });
    }
    return this.workletEngineStarting;
  }

  private applyPostInitOptions(): void {
    if (!this.module) return;

    this.scriptPanel?.attach(this.module);
    // QWERTY and MIDI-in notes carry their start time while the AudioWorklet engine plays.
    this.live.setAudioClock(() => (this.workletEngine?.isRunning() ? this.workletEngine.getContext() : null));
    if (this.live.enableClockIfRequested()) console.log('MIDI clock out enabled (?midiClock=1)');
    // Graph notes (sequencers, drums, arp...) follow the selected MIDI port, delayed by the queued audio.
    this.live.setGraphNoteLatency(() => this.queuedAudioMs());
    this.live.startGraphNotes();

    const renderTestScene = getRenderTestScene();
    if (renderTestScene > 0) {
      this.module._bespoke_set_render_test_mode(renderTestScene);
      console.log(`Render test mode: scene ${renderTestScene} loaded (?renderTest=${renderTestScene})`);
    }

    const bundledPatch = BUNDLED_PATCHES[new URLSearchParams(window.location.search).get('patch') ?? ''];
    if (bundledPatch && !loadBundledLayout(this.module, bundledPatch)) {
      console.warn(`Bundled patch ${bundledPatch} could not be loaded`);
    }

    const activeBackend = this.module._bespoke_get_renderer_backend() === 1 ? 'webgl' : 'webgpu';
    this.rendererBackend = activeBackend;
    publishRendererBreadcrumbs(activeBackend, this.rendererFallbackReason);

    const debugSelect = document.getElementById('webglDebugSelect') as HTMLSelectElement | null;
    if (debugSelect) debugSelect.disabled = activeBackend !== 'webgl';

    const rendererSelect = document.getElementById('rendererSelect') as HTMLSelectElement | null;
    if (rendererSelect) rendererSelect.value = activeBackend;

    const backendIds: Record<AudioBackendId, number> = { worklet: AUDIO_BACKEND_WORKLET_WASM, ring: 1, sdl: 0 };
    this.module._bespoke_set_audio_backend_id(backendIds[this.audioBackendId]);
    const choice = this.audioBackendChoice;
    if (choice.backend === 'worklet') {
      console.log(`Audio backend: worklet engine (${choice.source}) — starts on Play`);
      // Compile the engine's .wasm while the user looks at the canvas; Play then only instantiates.
      compileWorkletEngine().catch(() => undefined);
    } else if (choice.backend === 'ring') {
      console.log(`Audio backend: worklet ring, main-thread DSP (${choice.source}) — starts on Play`);
    } else if (choice.fallbackReason) {
      const message = `Audio: SDL2 fallback — AudioWorklet unavailable: ${choice.fallbackReason}`;
      console.warn(message);
      this.patchStatusHelpers.showPatchStatus(message, 8000);
    } else {
      console.log(`Audio backend: SDL2 (${choice.source})`);
    }

    if (isAudioWorkletPocRequested()) {
      // Debug-only probe: loaded on demand so the main bundle does not carry it.
      void import('../audio/workletPoc').then(({ runAudioWorkletPoc }) => runAudioWorkletPoc()).then((result) => {
        console.log('AudioWorklet POC result:', result);
        (window as unknown as Record<string, unknown>).__bespokeAudioWorkletPoc = result;
        this.module?._bespoke_set_audio_backend_id(2);
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
      this.patchStatusHelpers.showPatchStatus(`Loaded shared patch${placeholderNotice(this.module)}`);
    }
  }

  /** Samples the graph wants back after the store evicted them (e.g. undo of a deleted sampler). */
  private async restoreMissingSamples(): Promise<void> {
    const module = this.module;
    if (!module) return;
    const hashes = JSON.parse(module.UTF8ToString(module._bespoke_take_missing_samples())) as string[];
    if (hashes.length === 0) return;
    await restorePatchSamples(module, hashes);
    if (this.module === module) module._bespoke_samples_restored();
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

  /** Audio queued ahead of the speakers; 0 when SDL plays (its buffer is a few ms). */
  private queuedAudioMs(): number {
    // The worklet engine renders one quantum ahead; only the device latency remains.
    if (this.workletEngine?.isRunning()) return this.workletEngine.outputLatencySeconds() * 1000;
    const ring = this.workletRing;
    const context = ring?.getContext();
    const stats = ring?.getStats();
    if (!ring || !ring.isRunning() || !context || !stats) return 0;
    return (stats.depthFrames / context.sampleRate) * 1000;
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
      // The mic feeds whichever instance plays the graph, so settle that first.
      const engine = (await this.ensureWorkletEngine()) ? this.workletEngine : null;
      const ring = engine?.getInputRing();
      this.inputCapture = await startInputCapture(
        this.module,
        engine && ring
          ? { ring, setActive: (active) => engine.setInputActive(active), context: engine.getContext() }
          : { context: this.workletRing?.getContext() },
      );
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

    if (!this.isProcessingEvents) {
      this.isProcessingEvents = true;
      try {
        this.module._bespoke_process_events();
      } finally {
        this.isProcessingEvents = false;
      }
    }

    const state = this.module._bespoke_get_init_state();

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
    const cppError = this.module?._bespoke_get_init_error();
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
      this.cssWidth = container.clientWidth || 1;
      this.cssHeight = container.clientHeight || 1;

      // Size the backing store in physical pixels so rendering is sharp on
      // HiDPI displays; #canvas is styled at width/height:100% so the on-page
      // display size tracks the container regardless of this attribute.
      const dpr = window.devicePixelRatio || 1;
      this.canvas.width = Math.max(1, Math.round(this.cssWidth * dpr));
      this.canvas.height = Math.max(1, Math.round(this.cssHeight * dpr));

      if (this.isInitialized && this.module) {
        this.module._bespoke_resize(this.cssWidth, this.cssHeight);
      }
    }
  }

  /**
   * WebGPU device-lost callback (GPU driver reset, GPUDevice.destroy(), tab discard). The device
   * is unrecoverable, so the renderer moves to WebGL2 on a fresh canvas; the patch, audio and undo
   * history stay as they are.
   */
  private handleDeviceLost(): void {
    console.error('WebGPU device lost');
    this.handleContextLost('webgpu');
  }

  private handleContextLost(backend: RendererBackend): void {
    if (!this.isInitialized) {
      this.pendingContextLoss = backend;
      return;
    }
    if (this.recoveringRenderer) return;
    this.recoveringRenderer = true;
    this.renderLoop?.stop();
    this.renderLoop = null;
    // Leave the device-lost callback (and the C++ context that raised it) before tearing it down.
    window.setTimeout(() => void this.recoverRenderer(backend), 0);
  }

  private async recoverRenderer(lost: RendererBackend): Promise<void> {
    const delaysMs = [0, 500, 2000];
    for (const delay of delaysMs) {
      if (delay) await new Promise((resolve) => window.setTimeout(resolve, delay));
      if (!this.module || !this.canvas) break;
      this.replaceCanvasElement();
      this.resizeCanvas();
      if (this.module._bespoke_reinit_renderer(1) !== 0) continue;

      installCanvasInput(this.canvas, this.module);
      this.installCanvasExtras(this.canvas);
      this.module._bespoke_resize(this.cssWidth, this.cssHeight);
      this.rendererBackend = 'webgl';
      this.rendererFallbackReason =
        lost === 'webgpu' ? 'WebGPU device lost; switched to WebGL2' : 'WebGL2 context lost; restored on a new context';
      publishRendererBreadcrumbs('webgl', this.rendererFallbackReason);
      if (lost === 'webgpu') updateFallbackBadge(this.rendererFallbackReason);
      const rendererSelect = document.getElementById('rendererSelect') as HTMLSelectElement | null;
      if (rendererSelect) rendererSelect.value = 'webgl';
      this.startRenderLoopOnly();
      this.recoveringRenderer = false;
      console.warn(this.rendererFallbackReason);
      this.patchStatusHelpers.showPatchStatus(`Graphics: ${this.rendererFallbackReason}`, 6000);
      return;
    }
    this.showRecoverableError(
      'Graphics Device Lost',
      'The GPU connection was lost (driver reset or tab suspended) and could not be restored. Reload to continue.',
    );
  }

  private showRecoverableError(header: string, detail: string): void {
    const statusEl = document.getElementById('status');
    const headerEl = statusEl?.querySelector('.status-header');
    const subheaderEl = statusEl?.querySelector('.status-subheader');
    if (statusEl) {
      statusEl.classList.remove('hidden');
      statusEl.classList.add('error');
    }
    if (headerEl) headerEl.textContent = header;
    if (subheaderEl) subheaderEl.textContent = detail;
    if (statusEl && !statusEl.querySelector('.init-recovery')) {
      const recovery = document.createElement('div');
      recovery.className = 'init-recovery';
      recovery.innerHTML = '<button type="button" class="btn init-retry-btn">Reload</button>';
      recovery.querySelector('.init-retry-btn')?.addEventListener('click', () => {
        window.location.reload();
      });
      statusEl.appendChild(recovery);
    }
  }

  /**
   * Moving the window to a monitor with another devicePixelRatio fires no resize; a
   * `(resolution: Xdppx)` query that stops matching does. Re-armed for the new ratio each time.
   */
  private watchDevicePixelRatio(): void {
    if (typeof window.matchMedia !== 'function') return;
    let query: MediaQueryList | null = null;
    const onChange = () => {
      this.resizeCanvas();
      arm();
    };
    const arm = () => {
      query?.removeEventListener('change', onChange);
      query = window.matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`);
      query.addEventListener('change', onChange, { once: true });
    };
    arm();
    this.disposers.push(() => query?.removeEventListener('change', onChange));
  }

  shutdown(): void {
    if (this.autoSaveTimer !== null) window.clearInterval(this.autoSaveTimer);
    this.autoSaveTimer = null;
    this.disposers.splice(0).forEach((dispose) => dispose());
    delete bespokeWindow.__bespoke_on_device_lost;
    delete bespokeWindow.__bespoke_on_canvas_notice;
    delete bespokeWindow.__bespoke_on_samples_missing;
    this.inputCapture?.stop();
    this.inputCapture = null;
    this.live.graphNotes.setSource(null);
    this.workletEngine?.dispose();
    this.workletEngine = null;
    this.workletRing?.dispose();
    this.workletRing = null;
    this.renderLoop?.stop();
    this.renderLoop = null;
    this.module?._bespoke_shutdown();
    this.isInitialized = false;
  }
}
