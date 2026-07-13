#include <emscripten.h>
#include <emscripten/html5.h>
#include <iostream>

// Forward declarations of your bespoke functions
extern "C" {
    void bespoke_resize(int width, int height);
    void bespoke_mouse_down(int x, int y, int button);
    void bespoke_mouse_up(int x, int y, int button);
    void bespoke_mouse_move(int x, int y);
    void bespoke_mouse_wheel(float x, float y);
    void bespoke_key_down(int key, int modifiers);
    void bespoke_key_up(int key, int modifiers);
    void bespoke_render(void);
}

// -------------------------------------------------------------------------
// Callback Functions (Defined strictly to avoid macro parsing errors)
// -------------------------------------------------------------------------

EM_BOOL on_resize(int eventType, const EmscriptenUiEvent* uiEvent, void* userData) {
    double cssWidth = 0;
    double cssHeight = 0;
    if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) == EMSCRIPTEN_RESULT_SUCCESS) {
        bespoke_resize(static_cast<int>(cssWidth), static_cast<int>(cssHeight));
    }
    return EM_TRUE;
}

EM_BOOL on_mouse_down(int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData) {
    bespoke_mouse_down(mouseEvent->targetX, mouseEvent->targetY, mouseEvent->button);
    return EM_TRUE;
}

EM_BOOL on_mouse_up(int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData) {
    bespoke_mouse_up(mouseEvent->targetX, mouseEvent->targetY, mouseEvent->button);
    return EM_TRUE;
}

EM_BOOL on_mouse_move(int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData) {
    bespoke_mouse_move(mouseEvent->targetX, mouseEvent->targetY);
    return EM_TRUE;
}

EM_BOOL on_wheel(int eventType, const EmscriptenWheelEvent* wheelEvent, void* userData) {
    bespoke_mouse_wheel(
        static_cast<float>(wheelEvent->deltaX),
        static_cast<float>(wheelEvent->deltaY)
    );
    return EM_TRUE; // Consume event to prevent browser scrolling
}

EM_BOOL on_key_down(int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData) {
    int modifiers = 0;
    if (keyEvent->shiftKey) modifiers |= 1;
    if (keyEvent->altKey)   modifiers |= 2;
    if (keyEvent->ctrlKey)  modifiers |= 4;
    if (keyEvent->metaKey)  modifiers |= 8;
    
    bespoke_key_down(keyEvent->keyCode, modifiers);
    return EM_TRUE; // Consume event
}

EM_BOOL on_key_up(int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData) {
    int modifiers = 0;
    if (keyEvent->shiftKey) modifiers |= 1;
    if (keyEvent->altKey)   modifiers |= 2;
    if (keyEvent->ctrlKey)  modifiers |= 4;
    if (keyEvent->metaKey)  modifiers |= 8;

    bespoke_key_up(keyEvent->keyCode, modifiers);
    return EM_TRUE;
}

// -------------------------------------------------------------------------
// Main Entry Point
// -------------------------------------------------------------------------

EM_BOOL on_animation_frame(double time, void* userData) {
    (void)userData;
#if defined(BESPOKE_WASM_FRAME_INSTRUMENTATION)
    static double lastReportTime = 0.0;
    static unsigned int framesSinceReport = 0;
    ++framesSinceReport;
    if (lastReportTime == 0.0)
        lastReportTime = time;
    if (time - lastReportTime >= 1000.0) {
        std::cout << "BespokeSynth Profile: " << framesSinceReport
                  << " frames in " << (time - lastReportTime) << "ms" << std::endl;
        lastReportTime = time;
        framesSinceReport = 0;
    }
#else
    (void)time;
#endif
    bespoke_render();
    return EM_TRUE;
}

int main() {
    // Register Resize
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, on_resize);

    // Register Mouse
    emscripten_set_mousedown_callback("#canvas", nullptr, true, on_mouse_down);
    emscripten_set_mouseup_callback("#canvas",   nullptr, true, on_mouse_up);
    emscripten_set_mousemove_callback("#canvas", nullptr, true, on_mouse_move);
    emscripten_set_wheel_callback("#canvas",     nullptr, true, on_wheel);

    // Register Keyboard
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, on_key_down);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,   nullptr, true, on_key_up);

    // Drive rendering via the Emscripten animation-frame loop (required for WebGPU canvas presentation)
    emscripten_request_animation_frame_loop(on_animation_frame, nullptr);

    // Keep the runtime alive
    emscripten_exit_with_live_runtime();
    return 0;
}
