/**
 * BespokeSynth WASM Web Application Entry Point
 * 
 * This is the main TypeScript entry point for the npm-buildable web app.
 * It loads the WASM module and sets up the BespokeSynth interface.
 */

import './styles.css';

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

  async init(): Promise<void> {
    console.log('Initializing BespokeSynth WASM...');

    // Get canvas element
    this.canvas = document.getElementById('canvas') as HTMLCanvasElement;
    if (!this.canvas) {
      throw new Error('Canvas element not found');
    }

    // Set canvas size
    this.resizeCanvas();
    window.addEventListener('resize', () => this.resizeCanvas());

    // Show loading screen
    this.showStatus('Loading WebAssembly module...');

    try {
      // Load WASM module
      this.module = await loadWasmModule();
      console.log('WASM module loaded successfully');

      // Initialize synth
      const sampleRate = 44100;
      const bufferSize = 512;
      const result = this.module._bespoke_init?.(
        this.canvas.width,
        this.canvas.height,
        sampleRate,
        bufferSize
      );

      if (result === 0) {
        this.isInitialized = true;
        this.showStatus('Ready!');
        this.setupEventListeners();
        this.startRenderLoop();
        console.log('BespokeSynth initialized successfully');
      } else if (result === 1) {
        // Initialization is pending asynchronously. Wait for the WASM callback.
        await new Promise<void>((resolve, reject) => {
          console.log('Setting __bespoke_on_init_complete to receive async init completion');
          let initTimeout = window.setTimeout(() => {
            delete (window as any).__bespoke_on_init_complete;
            if (pollInterval) clearInterval(pollInterval);
            console.error('Initialization timed out after 30 seconds waiting for WASM to complete');
            reject(new Error('Initialization timed out waiting for WASM to complete'));
          }, 30000); // Increased from 10000 to 30000 (30 seconds)

          // Poll for WebGPU events to ensure async callbacks are processed
          const pollInterval = window.setInterval(() => {
            if (this.module._bespoke_process_events) {
              this.module._bespoke_process_events();
            }
          }, 100); // Poll every 100ms

          (window as any).__bespoke_on_init_complete = (status: number) => {
            console.log('Resolving __bespoke_on_init_complete with status', status);
            window.clearTimeout(initTimeout);
            if (pollInterval) clearInterval(pollInterval);
            delete (window as any).__bespoke_on_init_complete;
            if (status === 0) resolve();
            else reject(new Error(`Initialization failed with code: ${status}`));
          };
        });

        // Now initialization completed successfully
        this.isInitialized = true;
        this.showStatus('Ready!');
        this.setupEventListeners();
        this.startRenderLoop();
        console.log('BespokeSynth initialized successfully (async)');
      } else {
        throw new Error(`Initialization failed with code: ${result}`);
      }
    } catch (error) {
      console.error('Failed to initialize BespokeSynth:', error);
      this.showStatus(`Error: ${error instanceof Error ? error.message : 'Unknown error'}`);
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
        // Use e.code for key position, fall back to keyCode for compatibility
        const keyCode = e.code ? e.code.charCodeAt(0) : (e.keyCode || e.which);
        this.module._bespoke_key_down(keyCode, modifiers);
      }
    });

    document.addEventListener('keyup', (e) => {
      if (this.module._bespoke_key_up) {
        const modifiers = this.getModifiers(e);
        // Use e.code for key position, fall back to keyCode for compatibility
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

  private showStatus(message: string): void {
    const statusEl = document.getElementById('status');
    if (statusEl) {
      statusEl.textContent = message;
      
      // Hide status after 3 seconds if it's a success message
      if (message === 'Ready!') {
        setTimeout(() => {
          statusEl.style.opacity = '0';
        }, 3000);
      }
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
