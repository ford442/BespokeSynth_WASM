/**
 * BespokeSynth WASM - Bridge API
 * Provides C-compatible API for JavaScript integration
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <emscripten.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialization
EMSCRIPTEN_KEEPALIVE int bespoke_init(int width, int height, int sampleRate, int bufferSize);
EMSCRIPTEN_KEEPALIVE void bespoke_process_events(void);
EMSCRIPTEN_KEEPALIVE void bespoke_shutdown(void);

// Audio processing
EMSCRIPTEN_KEEPALIVE void bespoke_process_audio(void);
EMSCRIPTEN_KEEPALIVE void bespoke_process_audio_block(float* output, int frames);
EMSCRIPTEN_KEEPALIVE void bespoke_set_sample_rate(int sampleRate);
EMSCRIPTEN_KEEPALIVE void bespoke_set_buffer_size(int bufferSize);
EMSCRIPTEN_KEEPALIVE int bespoke_get_sample_rate(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_buffer_size(void);

// Rendering
EMSCRIPTEN_KEEPALIVE void bespoke_set_frame_delta(float deltaSeconds);
EMSCRIPTEN_KEEPALIVE void bespoke_render(void);
EMSCRIPTEN_KEEPALIVE void bespoke_resize(int width, int height);

// Host-layer diagnostics (single input/render path verification)
EMSCRIPTEN_KEEPALIVE void bespoke_reset_host_counters(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_host_render_count(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_host_mouse_down_count(void);

// Input handling
EMSCRIPTEN_KEEPALIVE void bespoke_mouse_move(int x, int y);
EMSCRIPTEN_KEEPALIVE void bespoke_mouse_down(int x, int y, int button);
EMSCRIPTEN_KEEPALIVE void bespoke_mouse_up(int x, int y, int button);
EMSCRIPTEN_KEEPALIVE void bespoke_mouse_wheel(float deltaX, float deltaY);
EMSCRIPTEN_KEEPALIVE void bespoke_key_down(int keyCode, int modifiers);
EMSCRIPTEN_KEEPALIVE void bespoke_key_up(int keyCode, int modifiers);

// Module management
EMSCRIPTEN_KEEPALIVE int bespoke_create_module(const char* type, float x, float y);
EMSCRIPTEN_KEEPALIVE void bespoke_delete_module(int moduleId);
EMSCRIPTEN_KEEPALIVE void bespoke_connect_modules(int sourceId, int destId);
EMSCRIPTEN_KEEPALIVE int bespoke_get_view_mode(void);
EMSCRIPTEN_KEEPALIVE void bespoke_set_view_mode(int mode);
// Enable Mixer/Effects/Sequencer demo tabs (call before or after init).
// Default product surface is ModuleCanvas; pass 1 for ?legacyPanels=1.
EMSCRIPTEN_KEEPALIVE void bespoke_set_legacy_panels(int enabled);
EMSCRIPTEN_KEEPALIVE int bespoke_get_legacy_panels(void);

// Control access
EMSCRIPTEN_KEEPALIVE void bespoke_set_control_value(int moduleId, const char* controlName, float value);
EMSCRIPTEN_KEEPALIVE float bespoke_get_control_value(int moduleId, const char* controlName);

// Web MIDI input, normalized velocity/value range is [0, 1].
EMSCRIPTEN_KEEPALIVE void bespoke_midi_note_on(int channel, int pitch, float velocity);
EMSCRIPTEN_KEEPALIVE void bespoke_midi_note_off(int channel, int pitch);
EMSCRIPTEN_KEEPALIVE void bespoke_midi_cc(int channel, int cc, float value);

// State management
EMSCRIPTEN_KEEPALIVE int bespoke_save_state(const char* filename);
EMSCRIPTEN_KEEPALIVE int bespoke_load_state(const char* filename);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_state_json(void);
EMSCRIPTEN_KEEPALIVE int bespoke_load_state_json(const char* json);
// Loads a bundled WASM patch/layout from the preloaded /resource filesystem.
EMSCRIPTEN_KEEPALIVE int bespoke_load_layout(const char* path);

// Transport
EMSCRIPTEN_KEEPALIVE void bespoke_play(void);
EMSCRIPTEN_KEEPALIVE void bespoke_stop(void);
EMSCRIPTEN_KEEPALIVE void bespoke_set_tempo(float bpm);
EMSCRIPTEN_KEEPALIVE float bespoke_get_tempo(void);

// Debug/info
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_version(void);
EMSCRIPTEN_KEEPALIVE float bespoke_get_cpu_load(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_module_count(void);

// Audio backend / health (SDL default; external = AudioWorklet host)
// backend: 0 = SDL2, 1 = worklet (SAB ring), 2 = POC tone
EMSCRIPTEN_KEEPALIVE void bespoke_set_external_audio(int enabled);
EMSCRIPTEN_KEEPALIVE int bespoke_get_external_audio(void);
EMSCRIPTEN_KEEPALIVE void bespoke_set_external_sample_rate(float sampleRate);
EMSCRIPTEN_KEEPALIVE void bespoke_set_audio_backend_id(int backend);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_audio_health_json(void);
EMSCRIPTEN_KEEPALIVE void bespoke_reset_audio_health(void);
EMSCRIPTEN_KEEPALIVE void bespoke_set_audio_queue_stats(int depthFrames, int capacityFrames, double externalUnderruns);

// Panel management
EMSCRIPTEN_KEEPALIVE void bespoke_set_panel(int panelIndex);
EMSCRIPTEN_KEEPALIVE int bespoke_get_panel(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_panel_count(void);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_panel_name(int panelIndex);
EMSCRIPTEN_KEEPALIVE int bespoke_is_panel_loaded(int panelIndex);
EMSCRIPTEN_KEEPALIVE int bespoke_is_panel_running(int panelIndex);
EMSCRIPTEN_KEEPALIVE int bespoke_get_panel_frame_count(int panelIndex);
EMSCRIPTEN_KEEPALIVE void bespoke_log_all_panels_status(void);

// Initialization state queries
EMSCRIPTEN_KEEPALIVE int bespoke_get_init_state(void);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_init_error(void);
EMSCRIPTEN_KEEPALIVE int bespoke_is_fully_initialized(void);

// Control enumeration / inspection API
// Returns the total number of enumerable controls (knobs, sliders, etc.)
EMSCRIPTEN_KEEPALIVE int bespoke_get_control_count(void);
// Returns a JSON string describing the control at the given index:
//   { "id": <int>, "type": "knob", "label": <str>, "value": <float>,
//     "min": <float>, "max": <float>, "unit": <str>,
//     "x": <float>, "y": <float>, "size": <float> }
// Returns an empty object "{}" for out-of-range indices.
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_control_info(int index);

// Theming API – runtime color overrides
// colorId selects the theme slot (see BespokeWasm/Theme.h ThemeColorId enum).
// r, g, b, a are in [0, 1].
EMSCRIPTEN_KEEPALIVE void bespoke_set_theme_color(int colorId, float r, float g, float b, float a);
// Reset all overrides back to built-in defaults.
EMSCRIPTEN_KEEPALIVE void bespoke_reset_theme(void);

// Renderer backend selection (call before bespoke_init)
// backend: 0 = WebGPU (default), 1 = WebGL2
EMSCRIPTEN_KEEPALIVE void bespoke_set_renderer_backend(int backend);
EMSCRIPTEN_KEEPALIVE int bespoke_get_renderer_backend(void);

// UI font selection (call before bespoke_init; ?debugFont=pixel)
// mode: 0 = SDF atlas (default), 1 = 5x7 pixel font.
// bespoke_get_font_mode() reports what text actually uses: 1 when the pixel font
// was requested or the SDF atlas failed to load.
EMSCRIPTEN_KEEPALIVE void bespoke_set_font_mode(int mode);
EMSCRIPTEN_KEEPALIVE int bespoke_get_font_mode(void);

// WebGL2 capability queries (safe before bespoke_init)
EMSCRIPTEN_KEEPALIVE int bespoke_is_webgl2_supported(void);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_webgl2_error(void);
EMSCRIPTEN_KEEPALIVE void bespoke_set_webgl_preserve_drawing_buffer(int enabled);

// WebGL2-only debug modes (see WebGLDebugMode in Renderer2D.h)
EMSCRIPTEN_KEEPALIVE void bespoke_set_webgl_debug_mode(int mode);
EMSCRIPTEN_KEEPALIVE int bespoke_get_webgl_debug_mode(void);

// Returns active backend type after capture attempt.
//  1 = WebGL2 (use canvas.toDataURL in JS)
//  0 = WebGPU (RGBA pixels via bespoke_get_screenshot_pixels)
// -1 = error
EMSCRIPTEN_KEEPALIVE int bespoke_capture_screenshot(int* outWidth, int* outHeight);
EMSCRIPTEN_KEEPALIVE const unsigned char* bespoke_get_screenshot_pixels(int* outByteLength);

// Canonical render-test scene for visual regression (resets user modules)
/** scene: 0 off, 1 canonical synth voice, 2 drumseq → mixer → compressor. */
EMSCRIPTEN_KEEPALIVE void bespoke_set_render_test_mode(int scene);
/** Active render-test scene id (0 when off). */
EMSCRIPTEN_KEEPALIVE int bespoke_get_render_test_mode(void);

// Font regression panel (visual test overlay for pixel font metrics/glyphs)
EMSCRIPTEN_KEEPALIVE void bespoke_set_font_test_visible(int visible);
EMSCRIPTEN_KEEPALIVE int bespoke_get_font_test_visible(void);

// Sample I/O, live input, and offline render
EMSCRIPTEN_KEEPALIVE int bespoke_load_sample(const unsigned char* bytes, int len, const char* name);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_sample_hash(int sampleId);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_sample_name(int sampleId);
EMSCRIPTEN_KEEPALIVE int bespoke_assign_sample(int moduleId, const char* hash);
EMSCRIPTEN_KEEPALIVE int bespoke_find_first_module(const char* type);
EMSCRIPTEN_KEEPALIVE int bespoke_set_module_string(int moduleId, const char* key, const char* value);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_module_string(int moduleId, const char* key);
EMSCRIPTEN_KEEPALIVE void bespoke_push_input_audio(const float* frames, int framesCount);
// Whether the host's microphone capture is running (drives the Input module's status line).
EMSCRIPTEN_KEEPALIVE void bespoke_set_input_capture_active(int active);
EMSCRIPTEN_KEEPALIVE void bespoke_set_offline_format(int bitsPerSample);
EMSCRIPTEN_KEEPALIVE int bespoke_render_offline(double seconds, int sampleRate);
EMSCRIPTEN_KEEPALIVE const unsigned char* bespoke_get_offline_wav(int* outByteLength);
EMSCRIPTEN_KEEPALIVE int bespoke_get_offline_wav_size(void);
EMSCRIPTEN_KEEPALIVE void bespoke_free_offline_render(void);

// Pointer input (mouse, touch, pen). modifiers: 1 shift, 2 alt, 4 ctrl, 8 meta.
// The first pointer down owns the canvas gesture until up/cancel.
EMSCRIPTEN_KEEPALIVE void bespoke_pointer_down(int pointerId, int x, int y, int button, int modifiers);
EMSCRIPTEN_KEEPALIVE void bespoke_pointer_move(int pointerId, int x, int y);
EMSCRIPTEN_KEEPALIVE void bespoke_pointer_up(int pointerId, int x, int y, int button);
EMSCRIPTEN_KEEPALIVE void bespoke_pointer_cancel(int pointerId);
EMSCRIPTEN_KEEPALIVE void bespoke_long_press(int x, int y);
EMSCRIPTEN_KEEPALIVE void bespoke_canvas_zoom(float factor, float centerX, float centerY);
EMSCRIPTEN_KEEPALIVE void bespoke_canvas_pan(float dx, float dy);
// 1 while the canvas owns typing (spawn-menu search); QWERTY piano stays off then.
EMSCRIPTEN_KEEPALIVE int bespoke_is_text_entry_active(void);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_module_info_json(int moduleId);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_modules_json(void);

// MIDI channel filter and CC learn (maps persist in patch JSON "midi.cc").
EMSCRIPTEN_KEEPALIVE void bespoke_set_midi_note_channel(int channel);
EMSCRIPTEN_KEEPALIVE int bespoke_get_midi_note_channel(void);
EMSCRIPTEN_KEEPALIVE int bespoke_midi_learn_arm(int moduleId, const char* controlName);
EMSCRIPTEN_KEEPALIVE int bespoke_midi_learn_arm_at(int x, int y);
EMSCRIPTEN_KEEPALIVE void bespoke_midi_learn_cancel(void);
EMSCRIPTEN_KEEPALIVE int bespoke_midi_learn_get_module(void);
EMSCRIPTEN_KEEPALIVE const char* bespoke_midi_learn_get_control(void);
EMSCRIPTEN_KEEPALIVE int bespoke_midi_cc_unbind(int moduleId, const char* controlName);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_midi_cc_map_json(void);

// Undo / redo (structured command stack, 50 steps)
EMSCRIPTEN_KEEPALIVE int bespoke_undo(void);
EMSCRIPTEN_KEEPALIVE int bespoke_redo(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_undo_depth(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_redo_depth(void);
EMSCRIPTEN_KEEPALIVE void bespoke_clear_undo_history(void);

// Scala tuning on the Scale module. kind: "scl" or "kbm"; empty text clears.
// Returns 1 when the text parsed (0 keeps 12-TET; see bespoke_scale_get_tuning_error).
EMSCRIPTEN_KEEPALIVE int bespoke_scale_set_tuning(const char* kind, const char* text, const char* name);
EMSCRIPTEN_KEEPALIVE const char* bespoke_scale_get_tuning_error(void);
EMSCRIPTEN_KEEPALIVE float bespoke_get_note_frequency(int pitch);

// Graph notes for the MIDI output: sequencer / drum / script / arp / chorder notes the engine plays.
EMSCRIPTEN_KEEPALIVE void bespoke_set_graph_note_capture(int enabled);
// Packed ints: pitch | velocity(0-127) << 8 | channel << 16 | isNoteOn << 20. Returns the count.
EMSCRIPTEN_KEEPALIVE int bespoke_drain_graph_notes(int* out, int maxEvents);
// 1 when a Note processor listens to the performance (QWERTY / MIDI in) instead of a cable.
EMSCRIPTEN_KEEPALIVE int bespoke_performance_notes_processed(void);
// Saved modules of types this build lacks, kept as labelled placeholders.
EMSCRIPTEN_KEEPALIVE int bespoke_get_placeholder_count(void);

// Script module host (?script=1). The TS worker host is the only caller.
EMSCRIPTEN_KEEPALIVE void bespoke_set_scripting_enabled(int enabled);
// 0 rejected, 1 queued on the script's own cables, 2 played on the performance path.
EMSCRIPTEN_KEEPALIVE int bespoke_script_note(int moduleId, int pitch, float velocity, int isNoteOn);
EMSCRIPTEN_KEEPALIVE int bespoke_script_set_control(int moduleId, const char* controlName, float value);
EMSCRIPTEN_KEEPALIVE void bespoke_script_stopped(int moduleId);
EMSCRIPTEN_KEEPALIVE double bespoke_get_transport_beat(void);
EMSCRIPTEN_KEEPALIVE int bespoke_is_transport_playing(void);
// Latest output samples from the analyzer ring (diagnostics / e2e pitch checks).
EMSCRIPTEN_KEEPALIVE int bespoke_copy_output_waveform(float* destination, int count);
// Live analyzer diagnostics: scalar FFT runs on the render thread vs WebGPU compute dispatches.
EMSCRIPTEN_KEEPALIVE int bespoke_get_analyzer_cpu_fft_count(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_analyzer_gpu_fft_count(void);

#ifdef __cplusplus
}
#endif
