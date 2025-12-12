/**
 * BespokeSynth WASM - Main TypeScript Entry Point
 * 
 * This file serves as the main entry point for the web application.
 * It initializes the AssemblyScript WASM module and sets up the UI.
 */

import { instantiate } from '@assemblyscript/loader';

interface WasmExports {
  add: (a: number, b: number) => number;
  multiply: (a: number, b: number) => number;
  fibonacci: (n: number) => number;
  sineOscillator: (phase: number) => number;
  calculateRMS: (bufferPtr: number, length: number) => number;
  lowPassCoefficient: (cutoff: number) => number;
  applyGain: (input: number, targetGain: number, currentGain: number, smoothing: number) => number;
}

class BespokeSynthApp {
  private wasmModule: any = null;
  private statusElement: HTMLElement | null = null;
  private outputElement: HTMLElement | null = null;

  constructor() {
    this.init();
  }

  private async init(): Promise<void> {
    console.log('🎵 Initializing BespokeSynth WASM...');
    
    this.statusElement = document.getElementById('status');
    this.outputElement = document.getElementById('output');

    try {
      await this.loadWasm();
      this.setupUI();
      this.updateStatus('ready', 'WASM Module Ready! 🚀');
      this.log('✅ BespokeSynth WASM initialized successfully!');
      this.log('Try the buttons above to test WASM functions.');
    } catch (error) {
      this.updateStatus('error', 'Failed to load WASM');
      this.log(`❌ Error: ${error}`);
      console.error('Failed to initialize WASM:', error);
    }
  }

  private async loadWasm(): Promise<void> {
    try {
      // Load the WASM module using AssemblyScript loader
      const wasmModule = await instantiate(
        fetch('release.wasm')
      );
      
      this.wasmModule = wasmModule.exports;
      
      console.log('✅ WASM module loaded successfully');
    } catch (error) {
      console.error('Failed to load WASM module:', error);
      throw new Error('WASM module failed to load');
    }
  }

  private setupUI(): void {
    const addBtn = document.getElementById('addBtn');
    const multiplyBtn = document.getElementById('multiplyBtn');
    const input1 = document.getElementById('input1') as HTMLInputElement;
    const input2 = document.getElementById('input2') as HTMLInputElement;

    if (addBtn) {
      addBtn.addEventListener('click', () => {
        const a = parseInt(input1?.value || '0');
        const b = parseInt(input2?.value || '0');
        const result = this.wasmModule?.add(a, b);
        this.log(`➕ WASM Add: ${a} + ${b} = ${result}`);
      });
    }

    if (multiplyBtn) {
      multiplyBtn.addEventListener('click', () => {
        const a = parseInt(input1?.value || '0');
        const b = parseInt(input2?.value || '0');
        const result = this.wasmModule?.multiply(a, b);
        this.log(`✖️ WASM Multiply: ${a} × ${b} = ${result}`);
      });
    }
  }

  private updateStatus(type: 'loading' | 'ready' | 'error', message: string): void {
    if (this.statusElement) {
      this.statusElement.className = `status ${type}`;
      this.statusElement.textContent = message;
    }
  }

  private log(message: string): void {
    if (this.outputElement) {
      const timestamp = new Date().toLocaleTimeString();
      const p = document.createElement('p');
      p.textContent = `[${timestamp}] ${message}`;
      p.style.marginBottom = '8px';
      this.outputElement.appendChild(p);
      
      // Auto-scroll to bottom
      this.outputElement.scrollTop = this.outputElement.scrollHeight;
    }
    console.log(message);
  }
}

// Initialize the app when DOM is ready
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => {
    new BespokeSynthApp();
  });
} else {
  new BespokeSynthApp();
}
