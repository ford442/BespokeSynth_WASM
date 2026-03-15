/**
 * BespokeSynth WASM Web Application Entry Point
 * 
 * This is the main TypeScript entry point for the npm-buildable web app.
 * It loads the WASM module and sets up the BespokeSynth interface.
 */

import './styles.css';

// Initialization step definitions
interface InitStep {
  id: string;
  label: string;
  weight: number; // Contribution to total progress (0-100)
}

// Define all initialization steps with their approximate weights
const INIT_STEPS: InitStep[] = [
  { id: 'wasm_load', label: 'Loading WebAssembly module', weight: 10 },
  { id: 'webgpu_instance', label: 'Creating WebGPU instance', weight: 15 },
  { id: 'webgpu_surface', label: 'Creating WebGPU surface', weight: 10 },
  { id: 'webgpu_adapter', label: 'Requesting GPU adapter', weight: 15 },
  { id: 'webgpu_device', label: 'Acquiring GPU device', weight: 15 },
  { id: 'renderer_pipelines', label: 'Compiling shader pipelines', weight: 20 },
  { id: 'audio_init', label: 'Initializing audio backend', weight: 10 },
  { id: 'controls_create', label: 'Creating UI controls', weight: 5 },
];

// Load WASM module script dynamically
const loadWasmModule = async (canvas?: HTMLCanvasElement): Promise<any> => {
  return new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = 'wasm/BespokeSynthWASM.js';
    script.onload = async () => {
      console.log('loadWasmModule: script loaded');
      // If the build was modularized, a factory function will be exposed
      const factory = (window as any).createBespokeSynth;
      console.log('loadWasmModule: factory type =', typeof factory);
      if (typeof factory === 'function') {
        const config = {
          canvas: canvas ?? document.getElementById('canvas'),
          print: (text: any) => console.log(text),
          printErr: (text: any) => console.error(text),
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
      if ((window as any).Module?.calledRun) {
        resolve((window as any).Module);
      } else {
        (window as any).Module = {
          ...(window as any).Module,
          onRuntimeInitialized: () => {
            console.log('loadWasmModule: onRuntimeInitialized called — resolving Module');
            resolve((window as any).Module);
          },
        };
      }
    };
    script.onerror = () => reject(new Error('Failed to load WASM module'));
    document.head.appendChild(script);
  });
};

// Main application class
class BespokeSynthApp {
  private canvas: HTMLCanvasElement | null = null;
  private module: any = null;
  private animationFrameId: number | null = null;
  private isInitialized = false;
  private currentProgress = 0;
  private completedSteps = new Set<string>();
  private activeStep: string | null = null;
  private initStartTime = 0;

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
    this.initializeProgressUI();

    try {
      // Step 1: Load WASM module
      this.setActiveStep('wasm_load');
      this.module = await loadWasmModule();
      this.completeStep('wasm_load');
      console.log('WASM module loaded successfully');

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
        this.showReadyState();
        this.setupEventListeners();
        this.startRenderLoop();
        console.log('BespokeSynth initialized successfully (sync)');
      } else if (result === 1) {
        // Initialization is pending asynchronously
        await this.waitForAsyncInit(statePollInterval);
      } else {
        throw new Error(`Initialization failed with code: ${result}`);
      }
    } catch (error) {
      console.error('Failed to initialize BespokeSynth:', error);
      this.showErrorState(error instanceof Error ? error.message : 'Unknown error');
    }
  }

  private initializeProgressUI(): void {
    const stepsContainer = document.getElementById('init-steps');
    if (!stepsContainer) return;

    stepsContainer.innerHTML = '';
    INIT_STEPS.forEach((step) => {
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
    INIT_STEPS.forEach(step => this.completeStep(step.id));
  }

  private updateProgress(): void {
    let progress = 0;
    
    // Add completed steps
    INIT_STEPS.forEach(step => {
      if (this.completedSteps.has(step.id)) {
        progress += step.weight;
      }
    });

    // Add partial progress for active step (50% of its weight)
    if (this.activeStep) {
      const activeStepData = INIT_STEPS.find(s => s.id === this.activeStep);
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

    // Process WebGPU events
    if (this.module._bespoke_process_events) {
      this.module._bespoke_process_events();
    }

    // Get current init state from C++
    const state = this.module._bespoke_get_init_state?.() ?? 0;
    
    // Map C++ InitState enum to our step IDs
    const stateToStep: Record<number, string> = {
      0: 'wasm_load',        // NotStarted
      1: 'webgpu_instance',  // WebGPURequested
      2: 'webgpu_adapter',   // WebGPUReady (adapter acquired)
      3: 'renderer_pipelines', // RendererReady
      4: 'audio_init',       // AudioReady
      5: 'controls_create',  // FullyInitialized
    };

    // Mark previous steps as completed based on current state
    const stateOrder = [0, 1, 2, 3, 4, 5];
    for (const s of stateOrder) {
      if (s < state && stateToStep[s]) {
        if (!this.completedSteps.has(stateToStep[s])) {
          this.completeStep(stateToStep[s]);
        }
      }
    }

    // Set active step for current state
    if (stateToStep[state] && this.activeStep !== stateToStep[state]) {
      this.setActiveStep(stateToStep[state]);
    }
  }

  private async waitForAsyncInit(statePollInterval: number): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      console.log('Waiting for async initialization...');
      
      const initTimeout = window.setTimeout(() => {
        clearInterval(statePollInterval);
        clearInterval(pollInterval);
        delete (window as any).__bespoke_on_init_complete;
        
        const elapsed = ((performance.now() - this.initStartTime) / 1000).toFixed(1);
        console.error(`Initialization timed out after ${elapsed}s`);
        
        reject(new Error(`Initialization timed out after ${elapsed}s. Check console for details.`));
      }, 60000); // 60 second timeout

      // Poll more frequently for better responsiveness
      const pollInterval = window.setInterval(() => {
        if (this.module._bespoke_process_events) {
          this.module._bespoke_process_events();
        }
      }, 16); // ~60fps polling

      (window as any).__bespoke_on_init_complete = (status: number) => {
        console.log('__bespoke_on_init_complete called with status:', status);
        
        window.clearTimeout(initTimeout);
        clearInterval(pollInterval);
        clearInterval(statePollInterval);
        delete (window as any).__bespoke_on_init_complete;

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
      this.showReadyState();
      this.setupEventListeners();
      this.startRenderLoop();
      
      const elapsed = ((performance.now() - this.initStartTime) / 1000).toFixed(2);
      console.log(`BespokeSynth initialized successfully in ${elapsed}s`);
    });
  }

  private getInitErrorMessage(code: number): string {
    const messages: Record<number, string> = {
      [-1]: 'WebGPU initialization failed - browser may not support WebGPU',
      [-2]: 'Renderer initialization failed - shader compilation error',
      [-3]: 'Audio backend initialization failed',
      [-4]: 'Failed to start async WebGPU initialization',
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

    // Mouse events
    this.canvas.addEventListener('mousedown', (e) => {
      if (this.module._bespoke_mouse_down) {
        this.module._bespoke_mouse_down(e.offsetX, e.offsetY, e.button);
      }
    });

    this.canvas.addEventListener('mouseup', (e) => {
      if (this.module._bespoke_mouse_up) {
        this.module._bespoke_mouse_up(e.offsetX, e.offsetY, e.button);
      }
    });

    this.canvas.addEventListener('mousemove', (e) => {
      if (this.module._bespoke_mouse_move) {
        this.module._bespoke_mouse_move(e.offsetX, e.offsetY);
      }
    });

    this.canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      if (this.module._bespoke_mouse_wheel) {
        this.module._bespoke_mouse_wheel(e.deltaX, e.deltaY);
      }
    }, { passive: false });

    // Keyboard events
    document.addEventListener('keydown', (e) => {
      if (this.module._bespoke_key_down) {
        const modifiers = this.getModifiers(e);
        const keyCode = e.code ? e.code.charCodeAt(0) : (e.keyCode || e.which);
        this.module._bespoke_key_down(keyCode, modifiers);
      }
    });

    document.addEventListener('keyup', (e) => {
      if (this.module._bespoke_key_up) {
        const modifiers = this.getModifiers(e);
        const keyCode = e.code ? e.code.charCodeAt(0) : (e.keyCode || e.which);
        this.module._bespoke_key_up(keyCode, modifiers);
      }
    });

    // Control buttons
    const playBtn = document.getElementById('playBtn');
    const stopBtn = document.getElementById('stopBtn');

    if (playBtn) {
      playBtn.addEventListener('click', () => {
        if (this.module._bespoke_play) {
          this.module._bespoke_play();
        }
      });
    }

    if (stopBtn) {
      stopBtn.addEventListener('click', () => {
        if (this.module._bespoke_stop) {
          this.module._bespoke_stop();
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

    const renderFrame = () => {
      if (this.module?._bespoke_render) {
        this.module._bespoke_render();
      }
      this.animationFrameId = requestAnimationFrame(renderFrame);
    };

    this.animationFrameId = requestAnimationFrame(renderFrame);
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

  // Cleanup on page unload
  window.addEventListener('beforeunload', () => {
    app.shutdown();
  });
});

// Export for potential use as a library
export { BespokeSynthApp };
