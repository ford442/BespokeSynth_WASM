/**
 * BespokeSynth WASM - Bridge Implementation
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "WasmBridge.h"
#include "WebGPUContext.h"
#include "WebGPURenderer.h"
#include "SDL2AudioBackend.h"
#include "Knob.h"
#include <cstdio>
#include <string>
#include <memory>
#include <vector>

// Key code constants
static const int KEY_SHIFT = 16;
static const int KEY_SPACE = 32;

using namespace bespoke::wasm;

// Global state
static std::unique_ptr<WebGPUContext> gContext;
static std::unique_ptr<WebGPURenderer> gRenderer;
static std::unique_ptr<SDL2AudioBackend> gAudioBackend;

// Demo controls
static std::vector<std::unique_ptr<Knob>> gKnobs;

static int gWidth = 800;
static int gHeight = 600;
static bool gInitialized = false;

// Version string
static const char* kVersion = "1.0.0-wasm";

// Audio callback for demo
static void audioCallback(const float* const* input, float* const* output,
                          int numInputChannels, int numOutputChannels, int numSamples) {
    // Simple demo: generate a sine wave with frequency controlled by first knob
    static float phase = 0.0f;
    float frequency = 440.0f;
    
    if (!gKnobs.empty()) {
        frequency = 100.0f + gKnobs[0]->getValue() * 800.0f;  // 100-900 Hz
    }
    
    float phaseInc = 2.0f * 3.14159265f * frequency / gAudioBackend->getSampleRate();
    
    for (int i = 0; i < numSamples; i++) {
        float sample = sinf(phase) * 0.3f;  // Low amplitude for safety
        phase += phaseInc;
        if (phase > 2.0f * 3.14159265f) {
            phase -= 2.0f * 3.14159265f;
        }
        
        for (int ch = 0; ch < numOutputChannels; ch++) {
            output[ch][i] = sample;
        }
    }
}

extern "C" {

EMSCRIPTEN_KEEPALIVE int bespoke_init(int width, int height, int sampleRate, int bufferSize) {
    printf("BespokeSynth WASM: Initializing (%dx%d, %dHz, %d samples)\n", 
           width, height, sampleRate, bufferSize);
    
    gWidth = width;
    gHeight = height;
    
    // Initialize WebGPU context (asynchronous)
    gContext = std::make_unique<WebGPUContext>();

    bool started = gContext->initializeAsync("#canvas", [] (bool success) {
        if (!success) {
            printf("BespokeSynth WASM: Failed to initialize WebGPU\n");
            // Notify JS that initialization failed
            emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-1);");
            return;
        }

        // Continue remaining initialization on success
        gContext->resize(gWidth, gHeight);

        // Initialize renderer
        gRenderer = std::make_unique<WebGPURenderer>(*gContext);
        if (!gRenderer->initialize()) {
            printf("BespokeSynth WASM: Failed to initialize renderer\n");
            emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-2);");
            return;
        }

        // Initialize audio backend
        gAudioBackend = std::make_unique<SDL2AudioBackend>();
        if (!gAudioBackend->initialize(44100, 512, 2, 0)) {
            printf("BespokeSynth WASM: Failed to initialize audio\n");
            emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-3);");
            return;
        }

        gAudioBackend->setCallback(audioCallback);

        // Create some demo knobs
        gKnobs.clear();

        auto knob1 = std::make_unique<Knob>("Frequency", 0.5f);
        knob1->setRange(0.0f, 1.0f);
        knob1->setStyle(KnobStyle::Classic);
        knob1->setColors(
            Color(0.25f, 0.25f, 0.28f, 1.0f),
            Color(0.7f, 0.7f, 0.75f, 1.0f),
            Color(0.4f, 0.8f, 0.5f, 1.0f)
        );
        gKnobs.push_back(std::move(knob1));

        auto knob2 = std::make_unique<Knob>("Volume", 0.7f);
        knob2->setRange(0.0f, 1.0f);
        knob2->setStyle(KnobStyle::Modern);
        knob2->setColors(
            Color(0.2f, 0.2f, 0.22f, 1.0f),
            Color(0.6f, 0.6f, 0.65f, 1.0f),
            Color(0.3f, 0.7f, 0.9f, 1.0f)
        );
        gKnobs.push_back(std::move(knob2));

        auto knob3 = std::make_unique<Knob>("Filter", 0.3f);
        knob3->setRange(0.0f, 1.0f);
        knob3->setStyle(KnobStyle::LED);
        knob3->setColors(
            Color(0.15f, 0.15f, 0.18f, 1.0f),
            Color(0.5f, 0.5f, 0.55f, 1.0f),
            Color(0.9f, 0.4f, 0.2f, 1.0f)
        );
        gKnobs.push_back(std::move(knob3));

        auto knob4 = std::make_unique<Knob>("Pan", 0.5f);
        knob4->setRange(0.0f, 1.0f);
        knob4->setBipolar(true);
        knob4->setStyle(KnobStyle::Vintage);
        gKnobs.push_back(std::move(knob4));

        gInitialized = true;
        printf("BespokeSynth WASM: Initialization complete\n");

        // Notify JS that initialization finished successfully
        emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(0);");
    });

    if (!started) {
        printf("BespokeSynth WASM: Failed to start WebGPU initialization\n");
        return -1;
    }

    // Indicate initialization started asynchronously
    return 1;
}

EMSCRIPTEN_KEEPALIVE void bespoke_shutdown(void) {
    printf("BespokeSynth WASM: Shutting down\n");
    
    gKnobs.clear();
    
    if (gAudioBackend) {
        gAudioBackend->shutdown();
        gAudioBackend.reset();
    }
    
    gRenderer.reset();
    gContext.reset();
    
    gInitialized = false;
}

EMSCRIPTEN_KEEPALIVE void bespoke_process_audio(void) {
    // Audio is handled by SDL callback, nothing to do here
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_sample_rate(int sampleRate) {
    // Would need to reinitialize audio for this to take effect
    printf("BespokeSynth WASM: Sample rate change requested: %d\n", sampleRate);
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_buffer_size(int bufferSize) {
    printf("BespokeSynth WASM: Buffer size change requested: %d\n", bufferSize);
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_sample_rate(void) {
    return gAudioBackend ? gAudioBackend->getSampleRate() : 44100;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_buffer_size(void) {
    return gAudioBackend ? gAudioBackend->getBufferSize() : 512;
}

EMSCRIPTEN_KEEPALIVE void bespoke_render(void) {
    if (!gInitialized || !gRenderer) return;
    
    gRenderer->beginFrame(gWidth, gHeight, 1.0f);
    
    // Clear background
    gRenderer->fillColor(Color(0.12f, 0.12f, 0.14f, 1.0f));
    gRenderer->rect(0, 0, static_cast<float>(gWidth), static_cast<float>(gHeight));
    gRenderer->fill();
    
    // Draw title
    gRenderer->fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
    gRenderer->fontSize(24.0f);
    gRenderer->text(20, 40, "BespokeSynth WASM - WebGPU Demo");
    
    // Draw knobs in a row
    float knobSize = 80.0f;
    float startX = 100.0f;
    float startY = 150.0f;
    float spacing = 120.0f;
    
    for (size_t i = 0; i < gKnobs.size(); i++) {
        float x = startX + i * spacing;
        gKnobs[i]->render(*gRenderer, x, startY, knobSize);
    }
    
    // Draw cables connecting knobs
    if (gKnobs.size() >= 2) {
        gRenderer->drawCableWithSag(
            startX, startY + knobSize * 0.5f + 20,
            startX + spacing, startY + knobSize * 0.5f + 20,
            Color(0.8f, 0.3f, 0.2f, 0.9f),
            3.0f, 0.2f
        );
    }
    if (gKnobs.size() >= 3) {
        gRenderer->drawCableWithSag(
            startX + spacing, startY + knobSize * 0.5f + 30,
            startX + spacing * 2, startY + knobSize * 0.5f + 30,
            Color(0.2f, 0.6f, 0.8f, 0.9f),
            3.0f, 0.25f
        );
    }
    
    // Draw some UI elements
    float panelX = 50.0f;
    float panelY = 280.0f;
    float panelW = static_cast<float>(gWidth) - 100.0f;
    float panelH = 200.0f;
    
    // Module panel
    gRenderer->fillColor(Color(0.18f, 0.18f, 0.2f, 1.0f));
    gRenderer->roundedRect(panelX, panelY, panelW, panelH, 8.0f);
    gRenderer->fill();
    
    gRenderer->strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
    gRenderer->strokeWidth(1.0f);
    gRenderer->roundedRect(panelX, panelY, panelW, panelH, 8.0f);
    gRenderer->stroke();
    
    // Draw sliders
    float sliderX = panelX + 30;
    float sliderY = panelY + 50;
    
    gRenderer->drawSlider(sliderX, sliderY, 200, 20, 0.6f,
        Color(0.25f, 0.25f, 0.28f, 1.0f),
        Color(0.4f, 0.7f, 0.5f, 1.0f));
    
    gRenderer->drawSlider(sliderX, sliderY + 40, 200, 20, 0.3f,
        Color(0.25f, 0.25f, 0.28f, 1.0f),
        Color(0.5f, 0.6f, 0.9f, 1.0f));
    
    // Draw VU meters
    float vuX = panelX + panelW - 100;
    float vuY = panelY + 30;
    
    float audioLevel = gAudioBackend ? gAudioBackend->getOutputLevel() : 0.0f;
    
    gRenderer->drawVUMeter(vuX, vuY, 20, 140, audioLevel,
        Color(0.2f, 0.8f, 0.3f, 1.0f),
        Color(1.0f, 0.2f, 0.1f, 1.0f));
    
    gRenderer->drawVUMeter(vuX + 30, vuY, 20, 140, audioLevel * 0.9f,
        Color(0.2f, 0.8f, 0.3f, 1.0f),
        Color(1.0f, 0.2f, 0.1f, 1.0f));
    
    // Draw status
    gRenderer->fillColor(Color(0.6f, 0.6f, 0.65f, 1.0f));
    gRenderer->fontSize(12.0f);
    
    char statusText[256];
    snprintf(statusText, sizeof(statusText), 
             "Sample Rate: %d Hz | Buffer: %d | Audio: %s",
             bespoke_get_sample_rate(),
             bespoke_get_buffer_size(),
             (gAudioBackend && gAudioBackend->isRunning()) ? "Running" : "Stopped");
    
    gRenderer->text(20, static_cast<float>(gHeight) - 20, statusText);
    
    gRenderer->endFrame();
}

EMSCRIPTEN_KEEPALIVE void bespoke_resize(int width, int height) {
    gWidth = width;
    gHeight = height;
    
    if (gContext) {
        gContext->resize(width, height);
    }
    
    printf("BespokeSynth WASM: Resized to %dx%d\n", width, height);
}

static int gMouseX = 0;
static int gMouseY = 0;
static bool gMouseDown = false;

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_move(int x, int y) {
    int prevX = gMouseX;
    int prevY = gMouseY;
    gMouseX = x;
    gMouseY = y;
    
    // Handle knob dragging
    if (gMouseDown) {
        for (auto& knob : gKnobs) {
            knob->onMouseDrag(static_cast<float>(x), static_cast<float>(y),
                              static_cast<float>(prevX), static_cast<float>(prevY));
        }
    }
}

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_down(int x, int y, int button) {
    gMouseDown = true;
    gMouseX = x;
    gMouseY = y;
    
    // Check knob hit testing
    float knobSize = 80.0f;
    float startX = 100.0f;
    float startY = 150.0f;
    float spacing = 120.0f;
    
    for (size_t i = 0; i < gKnobs.size(); i++) {
        float kx = startX + i * spacing;
        if (gKnobs[i]->hitTest(static_cast<float>(x), static_cast<float>(y), kx, startY, knobSize)) {
            gKnobs[i]->onMouseDown(static_cast<float>(x), static_cast<float>(y), kx, startY, knobSize);
            break;
        }
    }
}

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_up(int x, int y, int button) {
    gMouseDown = false;
    
    for (auto& knob : gKnobs) {
        knob->onMouseUp();
    }
}

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_wheel(float deltaX, float deltaY) {
    // Handle scroll on knobs
    float knobSize = 80.0f;
    float startX = 100.0f;
    float startY = 150.0f;
    float spacing = 120.0f;
    
    for (size_t i = 0; i < gKnobs.size(); i++) {
        float kx = startX + i * spacing;
        if (gKnobs[i]->hitTest(static_cast<float>(gMouseX), static_cast<float>(gMouseY), kx, startY, knobSize)) {
            gKnobs[i]->onScroll(deltaY);
            break;
        }
    }
}

EMSCRIPTEN_KEEPALIVE void bespoke_key_down(int keyCode, int modifiers) {
    // Handle shift for fine mode
    if (keyCode == KEY_SHIFT) {
        for (auto& knob : gKnobs) {
            knob->setFineMode(true);
        }
    }
    
    // Space to toggle audio
    if (keyCode == KEY_SPACE && gAudioBackend) {
        if (gAudioBackend->isRunning()) {
            gAudioBackend->stop();
        } else {
            gAudioBackend->start();
        }
    }
}

EMSCRIPTEN_KEEPALIVE void bespoke_key_up(int keyCode, int modifiers) {
    if (keyCode == KEY_SHIFT) {
        for (auto& knob : gKnobs) {
            knob->setFineMode(false);
        }
    }
}

EMSCRIPTEN_KEEPALIVE int bespoke_create_module(const char* type, float x, float y) {
    // TODO: Implement module creation
    printf("BespokeSynth WASM: Create module '%s' at (%.1f, %.1f)\n", type, x, y);
    return 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_delete_module(int moduleId) {
    printf("BespokeSynth WASM: Delete module %d\n", moduleId);
}

EMSCRIPTEN_KEEPALIVE void bespoke_connect_modules(int sourceId, int destId) {
    printf("BespokeSynth WASM: Connect %d -> %d\n", sourceId, destId);
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_control_value(int moduleId, const char* controlName, float value) {
    // For demo, control knobs directly
    if (moduleId >= 0 && moduleId < static_cast<int>(gKnobs.size())) {
        gKnobs[moduleId]->setValue(value);
    }
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_control_value(int moduleId, const char* controlName) {
    if (moduleId >= 0 && moduleId < static_cast<int>(gKnobs.size())) {
        return gKnobs[moduleId]->getValue();
    }
    return 0.0f;
}

EMSCRIPTEN_KEEPALIVE int bespoke_save_state(const char* filename) {
    printf("BespokeSynth WASM: Save state to '%s'\n", filename);
    return 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_load_state(const char* filename) {
    printf("BespokeSynth WASM: Load state from '%s'\n", filename);
    return 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_state_json(void) {
    // TODO: Return actual state
    return "{}";
}

EMSCRIPTEN_KEEPALIVE int bespoke_load_state_json(const char* json) {
    printf("BespokeSynth WASM: Load state from JSON\n");
    return 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_play(void) {
    if (gAudioBackend) {
        gAudioBackend->start();
    }
}

EMSCRIPTEN_KEEPALIVE void bespoke_stop(void) {
    if (gAudioBackend) {
        gAudioBackend->stop();
    }
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_tempo(float bpm) {
    printf("BespokeSynth WASM: Set tempo to %.1f BPM\n", bpm);
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_tempo(void) {
    return 120.0f;  // Default tempo
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_version(void) {
    return kVersion;
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_cpu_load(void) {
    // TODO: Calculate actual CPU load
    return 0.0f;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_module_count(void) {
    return static_cast<int>(gKnobs.size());
}

} // extern "C"
