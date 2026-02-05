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
EMSCRIPTEN_KEEPALIVE void bespoke_set_sample_rate(int sampleRate);
EMSCRIPTEN_KEEPALIVE void bespoke_set_buffer_size(int bufferSize);
EMSCRIPTEN_KEEPALIVE int bespoke_get_sample_rate(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_buffer_size(void);

// Rendering
EMSCRIPTEN_KEEPALIVE void bespoke_render(void);
EMSCRIPTEN_KEEPALIVE void bespoke_resize(int width, int height);

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

// Control access
EMSCRIPTEN_KEEPALIVE void bespoke_set_control_value(int moduleId, const char* controlName, float value);
EMSCRIPTEN_KEEPALIVE float bespoke_get_control_value(int moduleId, const char* controlName);

// State management
EMSCRIPTEN_KEEPALIVE int bespoke_save_state(const char* filename);
EMSCRIPTEN_KEEPALIVE int bespoke_load_state(const char* filename);
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_state_json(void);
EMSCRIPTEN_KEEPALIVE int bespoke_load_state_json(const char* json);

// Transport
EMSCRIPTEN_KEEPALIVE void bespoke_play(void);
EMSCRIPTEN_KEEPALIVE void bespoke_stop(void);
EMSCRIPTEN_KEEPALIVE void bespoke_set_tempo(float bpm);
EMSCRIPTEN_KEEPALIVE float bespoke_get_tempo(void);

// Debug/info
EMSCRIPTEN_KEEPALIVE const char* bespoke_get_version(void);
EMSCRIPTEN_KEEPALIVE float bespoke_get_cpu_load(void);
EMSCRIPTEN_KEEPALIVE int bespoke_get_module_count(void);

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

#ifdef __cplusplus
}
#endif
