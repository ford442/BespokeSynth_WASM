/**
 * BespokeSynth WASM - Main Entry Point
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include <emscripten.h>
#include <emscripten/html5.h>
#include "WasmBridge.h"
#include <cstdio>

// Main loop callback
static void mainLoop() {
    bespoke_render();
}

int main(int argc, char* argv[]) {
    printf("BespokeSynth WASM starting...\n");
    
    // Get canvas size from DOM
    int width = 800;
    int height = 600;
    
    double cssWidth, cssHeight;
    if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) == EMSCRIPTEN_RESULT_SUCCESS) {
        width = static_cast<int>(cssWidth);
        height = static_cast<int>(cssHeight);
    }
    
    // Initialize the synth
    int result = bespoke_init(width, height, 44100, 512);
    if (result != 0) {
        printf("BespokeSynth WASM: Failed to initialize (error %d)\n", result);
        return result;
    }
    
    // Set up canvas resize callback
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true,
        [](int eventType, const EmscriptenUiEvent* uiEvent, void* userData) -> int {
            double cssWidth, cssHeight;
            if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) == EMSCRIPTEN_RESULT_SUCCESS) {
                bespoke_resize(static_cast<int>(cssWidth), static_cast<int>(cssHeight));
            }
            return 0;
        });
    
    // Set up mouse callbacks
    emscripten_set_mousedown_callback("#canvas", nullptr, true,
        [](int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData) -> int {
            bespoke_mouse_down(mouseEvent->targetX, mouseEvent->targetY, mouseEvent->button);
            return 1;
        });
    
    emscripten_set_mouseup_callback("#canvas", nullptr, true,
        [](int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData) -> int {
            bespoke_mouse_up(mouseEvent->targetX, mouseEvent->targetY, mouseEvent->button);
            return 1;
        });
    
    emscripten_set_mousemove_callback("#canvas", nullptr, true,
        [](int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData) -> int {
            bespoke_mouse_move(mouseEvent->targetX, mouseEvent->targetY);
            return 1;
        });
    
    emscripten_set_wheel_callback("#canvas", nullptr, true,
        [](int eventType, const EmscriptenWheelEvent* wheelEvent, void* userData) -> int {
            bespoke_mouse_wheel(
                static_cast<float>(wheelEvent->deltaX),
                static_cast<float>(wheelEvent->deltaY)
            );
            return 1;
        });
    
    // Set up keyboard callbacks
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true,
        [](int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData) -> int {
            int modifiers = 0;
            if (keyEvent->shiftKey) modifiers |= 1;
            if (keyEvent->altKey) modifiers |= 2;
            if (keyEvent->ctrlKey) modifiers |= 4;
            if (keyEvent->metaKey) modifiers |= 8;
            bespoke_key_down(keyEvent->keyCode, modifiers);
            return 0;  // Don't consume event
        });
    
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true,
        [](int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData) -> int {
            int modifiers = 0;
            if (keyEvent->shiftKey) modifiers |= 1;
            if (keyEvent->altKey) modifiers |= 2;
            if (keyEvent->ctrlKey) modifiers |= 4;
            if (keyEvent->metaKey) modifiers |= 8;
            bespoke_key_up(keyEvent->keyCode, modifiers);
            return 0;
        });
    
    printf("BespokeSynth WASM: Starting main loop\n");
    
    // Start audio
    bespoke_play();
    
    // Start main loop (60 FPS target)
    emscripten_set_main_loop(mainLoop, 60, true);
    
    // Note: This point is never reached due to emscripten_set_main_loop
    return 0;
}
