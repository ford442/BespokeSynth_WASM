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
    _bespoke_process_audio_block(output: number, frames: number): void;
    _bespoke_set_sample_rate(sampleRate: number): void;
    _bespoke_set_buffer_size(bufferSize: number): void;
    _bespoke_get_sample_rate(): number;
    _bespoke_get_buffer_size(): number;

    // Rendering
    _bespoke_set_frame_delta(deltaSeconds: number): void;
    _bespoke_render(): void;
    _bespoke_resize(width: number, height: number): void;
    _bespoke_reset_host_counters(): void;
    _bespoke_get_host_render_count(): number;
    _bespoke_get_host_mouse_down_count(): number;

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
    _bespoke_get_view_mode(): number;
    _bespoke_set_view_mode(mode: number): void;
    _bespoke_set_legacy_panels(enabled: number): void;
    _bespoke_get_legacy_panels(): number;

    // Control access
    _bespoke_set_control_value(moduleId: number, controlName: number, value: number): void;
    _bespoke_get_control_value(moduleId: number, controlName: number): number;
    _bespoke_midi_note_on(channel: number, pitch: number, velocity: number): void;
    _bespoke_midi_note_off(channel: number, pitch: number): void;
    _bespoke_midi_cc(channel: number, cc: number, value: number): void;

    // State management
    _bespoke_save_state(filename: number): number;
    _bespoke_load_state(filename: number): number;
    _bespoke_get_state_json(): number;
    _bespoke_load_state_json(json: number): number;
    _bespoke_load_layout(path: number): number;

    // Transport
    _bespoke_play(): void;
    _bespoke_stop(): void;
    _bespoke_set_tempo(bpm: number): void;
    _bespoke_get_tempo(): number;

    // Debug/info
    _bespoke_get_version(): number;
    _bespoke_get_cpu_load(): number;
    _bespoke_get_module_count(): number;

    // Audio backend / health
    _bespoke_set_external_audio(enabled: number): void;
    _bespoke_get_external_audio(): number;
    _bespoke_set_external_sample_rate(sampleRate: number): void;
    _bespoke_set_audio_backend_id(backend: number): void;
    _bespoke_get_audio_health_json(): number;
    _bespoke_reset_audio_health(): void;
    _bespoke_set_audio_queue_stats(depthFrames: number, capacityFrames: number, externalUnderruns: number): void;

    // Panel management
    _bespoke_process_events(): void;
    _bespoke_set_panel(panelIndex: number): void;
    _bespoke_get_panel(): number;
    _bespoke_get_panel_count(): number;
    _bespoke_get_panel_name(panelIndex: number): number;
    _bespoke_is_panel_loaded(panelIndex: number): number;
    _bespoke_is_panel_running(panelIndex: number): number;
    _bespoke_get_panel_frame_count(panelIndex: number): number;
    _bespoke_log_all_panels_status(): void;

    // Initialization state
    _bespoke_get_init_state(): number;
    _bespoke_get_init_error(): number;
    _bespoke_is_fully_initialized(): number;

    // Control inspection / theming
    _bespoke_get_control_count(): number;
    _bespoke_get_control_info(index: number): number;
    _bespoke_set_theme_color(colorId: number, r: number, g: number, b: number, a: number): void;
    _bespoke_reset_theme(): void;

    // Renderer backend selection
    _bespoke_set_renderer_backend(backend: number): void;
    _bespoke_get_renderer_backend(): number;
    /** 0 = SDF atlas (default), 1 = 5x7 pixel font. Call before bespoke_init. */
    _bespoke_set_font_mode(mode: number): void;
    /** 1 when text uses the pixel font (requested, or the SDF atlas failed to load). */
    _bespoke_get_font_mode(): number;
    _bespoke_is_webgl2_supported(): number;
    _bespoke_get_webgl2_error(): number;
    _bespoke_set_webgl_preserve_drawing_buffer(enabled: number): void;
    _bespoke_set_webgl_debug_mode(mode: number): void;
    _bespoke_get_webgl_debug_mode(): number;

    // Screenshot / render-test capture
    _bespoke_capture_screenshot(outWidth: number, outHeight: number): number;
    _bespoke_get_screenshot_pixels(outByteLength: number): number;
    /** 0 off, 1 canonical synth voice, 2 drum bus. */
    _bespoke_set_render_test_mode(scene: number): void;
    /** Active render-test scene id (0 when off). */
    _bespoke_get_render_test_mode(): number;
    _bespoke_set_font_test_visible(visible: number): void;
    _bespoke_get_font_test_visible(): number;

    _bespoke_load_sample(bytes: number, len: number, name: number): number;
    _bespoke_get_sample_hash(sampleId: number): number;
    _bespoke_get_sample_name(sampleId: number): number;
    _bespoke_assign_sample(moduleId: number, hash: number): number;
    _bespoke_find_first_module(type: number): number;
    _bespoke_set_module_string(moduleId: number, key: number, value: number): number;
    _bespoke_get_module_string(moduleId: number, key: number): number;
    _bespoke_push_input_audio(frames: number, framesCount: number): void;
    _bespoke_set_offline_format(bitsPerSample: number): void;
    _bespoke_render_offline(seconds: number, sampleRate: number): number;
    _bespoke_get_offline_wav(outByteLength: number): number;
    _bespoke_get_offline_wav_size(): number;
    _bespoke_free_offline_render(): void;

    // Pointer input (mouse/touch/pen). modifiers: 1 shift, 2 alt, 4 ctrl, 8 meta.
    _bespoke_pointer_down(pointerId: number, x: number, y: number, button: number, modifiers: number): void;
    _bespoke_pointer_move(pointerId: number, x: number, y: number): void;
    _bespoke_pointer_up(pointerId: number, x: number, y: number, button: number): void;
    _bespoke_pointer_cancel(pointerId: number): void;
    _bespoke_long_press(x: number, y: number): void;
    _bespoke_canvas_zoom(factor: number, centerX: number, centerY: number): void;
    _bespoke_canvas_pan(dx: number, dy: number): void;
    _bespoke_is_text_entry_active(): number;
    /** JSON {id,type,x,y,width,height,screenX,screenY,scale}; "{}" when missing. */
    _bespoke_get_module_info_json(moduleId: number): number;
    /** JSON [{id,type}] */
    _bespoke_get_modules_json(): number;

    // MIDI channel filter + CC learn
    _bespoke_set_midi_note_channel(channel: number): void;
    _bespoke_get_midi_note_channel(): number;
    _bespoke_midi_learn_arm(moduleId: number, controlName: number): number;
    _bespoke_midi_learn_arm_at(x: number, y: number): number;
    _bespoke_midi_learn_cancel(): void;
    _bespoke_midi_learn_get_module(): number;
    _bespoke_midi_learn_get_control(): number;
    _bespoke_midi_cc_unbind(moduleId: number, controlName: number): number;
    /** JSON [{channel,cc,moduleId,control,min,max}] */
    _bespoke_get_midi_cc_map_json(): number;

    // Undo / redo
    _bespoke_undo(): number;
    _bespoke_redo(): number;
    _bespoke_get_undo_depth(): number;
    _bespoke_get_redo_depth(): number;
    _bespoke_clear_undo_history(): void;

    // Scala tuning (kind: "scl" | "kbm")
    _bespoke_scale_set_tuning(kind: number, text: number, name: number): number;
    _bespoke_scale_get_tuning_error(): number;
    _bespoke_get_note_frequency(pitch: number): number;

    // Script module host (?script=1)
    _bespoke_set_scripting_enabled(enabled: number): void;
    _bespoke_script_note(moduleId: number, pitch: number, velocity: number, isNoteOn: number): number;
    _bespoke_script_set_control(moduleId: number, controlName: number, value: number): number;
    _bespoke_script_stopped(moduleId: number): void;
    _bespoke_get_transport_beat(): number;
    _bespoke_is_transport_playing(): number;
    /** Copies the latest `count` output samples into a HEAPF32 buffer. */
    _bespoke_copy_output_waveform(destination: number, count: number): number;
    /** Scalar FFT runs on the render thread (CPU analyzer path). */
    _bespoke_get_analyzer_cpu_fft_count(): number;
    /** WebGPU compute FFT dispatches (analyzer_fft.wgsl). */
    _bespoke_get_analyzer_gpu_fft_count(): number;
}

/**
 * Emscripten Module interface
 */
interface EmscriptenModule {
    calledRun?: boolean;
    onRuntimeInitialized?: () => void;
    ccall?(
        ident: string,
        returnType: 'number' | 'string' | 'boolean' | null,
        argTypes: Array<'number' | 'string' | 'boolean'>,
        args: Array<number | string | boolean>
    ): number | string | boolean | null;
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
