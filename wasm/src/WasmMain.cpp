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
}

int main() {
    // 1. Resize Callback
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true,
        [](int eventType, const EmscriptenUiEvent* uiEvent, void* userData) -> EM_BOOL {
            double cssWidth, cssHeight;
            if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) == EMSCRIPTEN_RESULT_SUCCESS) {
                bespoke_resize(static_cast<int>(cssWidth), static_cast<int>(cssHeight));
            }
            return EM_TRUE;
        });

    // 2. Mouse Down
    emscripten_set_mousedown_callback("#canvas", nullptr, true,
        [](int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData) -> EM_BOOL {
            bespoke_mouse_down(mouseEvent->targetX, mouseEvent->targetY, mouseEvent->button);
            return EM_TRUE;
        });

    // 3. Mouse Up
    emscripten_set_mouseup_callback("#canvas", nullptr, true,
        [](int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData) -> EM_BOOL {
            bespoke_mouse_up(mouseEvent->targetX, mouseEvent->targetY, mouseEvent->button);
            return EM_TRUE;
        });

    // 4. Mouse Move
    emscripten_set_mousemove_callback("#canvas", nullptr, true,
        [](int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData) -> EM_BOOL {
            bespoke_mouse_move(mouseEvent->targetX, mouseEvent->targetY);
            return EM_TRUE;
        });

    // 5. Wheel
    emscripten_set_wheel_callback("#canvas", nullptr, true,
        [](int eventType, const EmscriptenWheelEvent* wheelEvent, void* userData) -> EM_BOOL {
            bespoke_mouse_wheel(
                static_cast<float>(wheelEvent->deltaX),
                static_cast<float>(wheelEvent->deltaY)
            );
            return EM_TRUE; // Consumes the event (prevents page scroll)
        });

    // 6. Key Down
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true,
        [](int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData) -> EM_BOOL {
            int modifiers = 0;
            if (keyEvent->shiftKey) modifiers |= 1;
            if (keyEvent->altKey) modifiers |= 2;
            if (keyEvent->ctrlKey) modifiers |= 4;
            if (keyEvent->metaKey) modifiers |= 8;
            
            bespoke_key_down(keyEvent->keyCode, modifiers);
            return EM_TRUE; // Return EM_TRUE to prevent default browser behavior
        });

    // 7. Key Up
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true,
        [](int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData) -> EM_BOOL {
            int modifiers = 0;
            if (keyEvent->shiftKey) modifiers |= 1;
            if (keyEvent->altKey) modifiers |= 2;
            if (keyEvent->ctrlKey) modifiers |= 4;
            if (keyEvent->metaKey) modifiers |= 8;

            bespoke_key_up(keyEvent->keyCode, modifiers);
            return EM_TRUE;
        });

    // Prevent main from exiting so callbacks keep working
    emscripten_exit_with_live_runtime();
    return 0;
}
