/**
 * BespokeSynth WASM TypeScript Definitions
 * Provides type-safe access to the WASM module from JavaScript/TypeScript
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

/**
 * BespokeSynth WASM Module interface
 */
export interface BespokeSynthModule extends EmscriptenModule {
    // Initialization
    _bespoke_init(width: number, height: number, sampleRate: number, bufferSize: number): number;
    _bespoke_shutdown(): void;

    // Audio processing
    _bespoke_process_audio(): void;
    _bespoke_set_sample_rate(sampleRate: number): void;
    _bespoke_set_buffer_size(bufferSize: number): void;
    _bespoke_get_sample_rate(): number;
    _bespoke_get_buffer_size(): number;

    // Rendering
    _bespoke_render(): void;
    _bespoke_resize(width: number, height: number): void;

    // Input handling
    _bespoke_mouse_move(x: number, y: number): void;
    _bespoke_mouse_down(x: number, y: number, button: number): void;
    _bespoke_mouse_up(x: number, y: number, button: number): void;
    _bespoke_mouse_wheel(deltaX: number, deltaY: number): void;
    _bespoke_key_down(keyCode: number, modifiers: number): void;
    _bespoke_key_up(keyCode: number, modifiers: number): void;

    // Module management
    _bespoke_create_module(type: number, x: number, y: number): number;
    _bespoke_delete_module(moduleId: number): void;
    _bespoke_connect_modules(sourceId: number, destId: number): void;

    // Control access
    _bespoke_set_control_value(moduleId: number, controlName: number, value: number): void;
    _bespoke_get_control_value(moduleId: number, controlName: number): number;

    // State management
    _bespoke_save_state(filename: number): number;
    _bespoke_load_state(filename: number): number;
    _bespoke_get_state_json(): number;
    _bespoke_load_state_json(json: number): number;

    // Transport
    _bespoke_play(): void;
    _bespoke_stop(): void;
    _bespoke_set_tempo(bpm: number): void;
    _bespoke_get_tempo(): number;

    // Debug/info
    _bespoke_get_version(): number;
    _bespoke_get_cpu_load(): number;
    _bespoke_get_module_count(): number;
}

/**
 * Key modifiers for keyboard events
 */
export enum KeyModifier {
    None = 0,
    Shift = 1,
    Alt = 2,
    Control = 4,
    Command = 8
}

/**
 * Mouse button constants
 */
export enum MouseButton {
    Left = 0,
    Middle = 1,
    Right = 2
}

/**
 * High-level TypeScript wrapper for BespokeSynth WASM
 */
export class BespokeSynth {
    private module: BespokeSynthModule;
    private canvas: HTMLCanvasElement;
    private animationFrameId: number | null = null;
    private isInitialized = false;

    constructor(module: BespokeSynthModule, canvas: HTMLCanvasElement) {
        this.module = module;
        this.canvas = canvas;
    }

    /**
     * Initialize the synth engine
     */
    async init(sampleRate = 44100, bufferSize = 512): Promise<boolean> {
        const result = this.module._bespoke_init(
            this.canvas.width,
            this.canvas.height,
            sampleRate,
            bufferSize
        );
        
        this.isInitialized = result === 0;
        
        if (this.isInitialized) {
            this.setupEventListeners();
        }
        
        return this.isInitialized;
    }

    /**
     * Shutdown the synth engine
     */
    shutdown(): void {
        this.stopRenderLoop();
        this.module._bespoke_shutdown();
        this.isInitialized = false;
    }

    /**
     * Start the render loop
     */
    startRenderLoop(): void {
        if (this.animationFrameId !== null) return;
        
        const renderFrame = () => {
            this.module._bespoke_render();
            this.animationFrameId = requestAnimationFrame(renderFrame);
        };
        
        this.animationFrameId = requestAnimationFrame(renderFrame);
    }

    /**
     * Stop the render loop
     */
    stopRenderLoop(): void {
        if (this.animationFrameId !== null) {
            cancelAnimationFrame(this.animationFrameId);
            this.animationFrameId = null;
        }
    }

    /**
     * Resize the canvas
     */
    resize(width: number, height: number): void {
        this.canvas.width = width;
        this.canvas.height = height;
        this.module._bespoke_resize(width, height);
    }

    /**
     * Start audio playback
     */
    play(): void {
        this.module._bespoke_play();
    }

    /**
     * Stop audio playback
     */
    stop(): void {
        this.module._bespoke_stop();
    }

    /**
     * Set the tempo in BPM
     */
    setTempo(bpm: number): void {
        this.module._bespoke_set_tempo(bpm);
    }

    /**
     * Get the current tempo in BPM
     */
    getTempo(): number {
        return this.module._bespoke_get_tempo();
    }

    /**
     * Get the sample rate
     */
    getSampleRate(): number {
        return this.module._bespoke_get_sample_rate();
    }

    /**
     * Get the buffer size
     */
    getBufferSize(): number {
        return this.module._bespoke_get_buffer_size();
    }

    /**
     * Get the version string
     */
    getVersion(): string {
        const ptr = this.module._bespoke_get_version();
        return this.module.UTF8ToString(ptr);
    }

    /**
     * Get the current CPU load (0-1)
     */
    getCpuLoad(): number {
        return this.module._bespoke_get_cpu_load();
    }

    /**
     * Get the number of modules
     */
    getModuleCount(): number {
        return this.module._bespoke_get_module_count();
    }

    /**
     * Create a new module
     */
    createModule(type: string, x: number, y: number): number {
        const typePtr = this.module.allocateUTF8(type);
        const moduleId = this.module._bespoke_create_module(typePtr, x, y);
        this.module._free(typePtr);
        return moduleId;
    }

    /**
     * Delete a module
     */
    deleteModule(moduleId: number): void {
        this.module._bespoke_delete_module(moduleId);
    }

    /**
     * Connect two modules
     */
    connectModules(sourceId: number, destId: number): void {
        this.module._bespoke_connect_modules(sourceId, destId);
    }

    /**
     * Set a control value
     */
    setControlValue(moduleId: number, controlName: string, value: number): void {
        const namePtr = this.module.allocateUTF8(controlName);
        this.module._bespoke_set_control_value(moduleId, namePtr, value);
        this.module._free(namePtr);
    }

    /**
     * Get a control value
     */
    getControlValue(moduleId: number, controlName: string): number {
        const namePtr = this.module.allocateUTF8(controlName);
        const value = this.module._bespoke_get_control_value(moduleId, namePtr);
        this.module._free(namePtr);
        return value;
    }

    /**
     * Save state to JSON string
     */
    getStateJson(): string {
        const ptr = this.module._bespoke_get_state_json();
        return this.module.UTF8ToString(ptr);
    }

    /**
     * Load state from JSON string
     */
    loadStateJson(json: string): boolean {
        const jsonPtr = this.module.allocateUTF8(json);
        const result = this.module._bespoke_load_state_json(jsonPtr);
        this.module._free(jsonPtr);
        return result === 0;
    }

    private setupEventListeners(): void {
        // Mouse events
        this.canvas.addEventListener('mousedown', (e) => {
            this.module._bespoke_mouse_down(e.offsetX, e.offsetY, e.button);
        });

        this.canvas.addEventListener('mouseup', (e) => {
            this.module._bespoke_mouse_up(e.offsetX, e.offsetY, e.button);
        });

        this.canvas.addEventListener('mousemove', (e) => {
            this.module._bespoke_mouse_move(e.offsetX, e.offsetY);
        });

        this.canvas.addEventListener('wheel', (e) => {
            e.preventDefault();
            this.module._bespoke_mouse_wheel(e.deltaX, e.deltaY);
        }, { passive: false });

        // Keyboard events
        document.addEventListener('keydown', (e) => {
            const modifiers = this.getModifiers(e);
            this.module._bespoke_key_down(e.keyCode, modifiers);
        });

        document.addEventListener('keyup', (e) => {
            const modifiers = this.getModifiers(e);
            this.module._bespoke_key_up(e.keyCode, modifiers);
        });

        // Resize handling
        const resizeObserver = new ResizeObserver((entries) => {
            for (const entry of entries) {
                const { width, height } = entry.contentRect;
                this.resize(Math.floor(width), Math.floor(height));
            }
        });
        
        resizeObserver.observe(this.canvas.parentElement || this.canvas);
    }

    private getModifiers(e: KeyboardEvent): number {
        let modifiers = KeyModifier.None;
        if (e.shiftKey) modifiers |= KeyModifier.Shift;
        if (e.altKey) modifiers |= KeyModifier.Alt;
        if (e.ctrlKey) modifiers |= KeyModifier.Control;
        if (e.metaKey) modifiers |= KeyModifier.Command;
        return modifiers;
    }
}

/**
 * Emscripten Module interface
 */
interface EmscriptenModule {
    UTF8ToString(ptr: number): string;
    allocateUTF8(str: string): number;
    _free(ptr: number): void;
    _malloc(size: number): number;
    HEAP8: Int8Array;
    HEAP16: Int16Array;
    HEAP32: Int32Array;
    HEAPU8: Uint8Array;
    HEAPU16: Uint16Array;
    HEAPU32: Uint32Array;
    HEAPF32: Float32Array;
    HEAPF64: Float64Array;
}

/**
 * Create and initialize BespokeSynth
 */
export async function createBespokeSynth(
    canvas: HTMLCanvasElement,
    options?: {
        sampleRate?: number;
        bufferSize?: number;
    }
): Promise<BespokeSynth> {
    // Wait for module to be ready
    const module = await new Promise<BespokeSynthModule>((resolve) => {
        if ((window as any).Module?.calledRun) {
            resolve((window as any).Module);
        } else {
            (window as any).Module = {
                ...(window as any).Module,
                onRuntimeInitialized: () => {
                    resolve((window as any).Module);
                }
            };
        }
    });

    const synth = new BespokeSynth(module, canvas);
    await synth.init(options?.sampleRate, options?.bufferSize);
    
    return synth;
}
