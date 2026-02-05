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
#include <atomic>

// Key code constants
static const int KEY_SHIFT = 16;
static const int KEY_SPACE = 32;

using namespace bespoke::wasm;

// Initialization state tracking
enum class InitState {
    NotStarted,
    WebGPURequested,
    WebGPUReady,
    RendererReady,
    AudioReady,
    FullyInitialized,
    Failed
};

// Global state
static std::unique_ptr<WebGPUContext> gContext;
static std::unique_ptr<WebGPURenderer> gRenderer;
static std::unique_ptr<SDL2AudioBackend> gAudioBackend;

// Demo controls
static std::vector<std::unique_ptr<Knob>> gKnobs;

static int gWidth = 800;
static int gHeight = 600;
static bool gInitialized = false;
static float gTime = 0.0f;
static InitState gInitState = InitState::NotStarted;
static std::string gInitErrorMessage;

// Thread safety for audio callback
static std::atomic<bool> gAudioCallbackActive{false};

// Panel selection
enum PanelType {
    PANEL_MIXER = 0,
    PANEL_EFFECTS,
    PANEL_SEQUENCER,
    PANEL_COUNT
};
static int gCurrentPanel = PANEL_MIXER;

// Panel status tracking
struct PanelStatus {
    bool loaded;
    bool running;
    int frameCount;
    float lastUpdateTime;
};
static PanelStatus gPanelStatus[PANEL_COUNT] = {
    {false, false, 0, 0.0f},  // Mixer
    {false, false, 0, 0.0f},  // Effects
    {false, false, 0, 0.0f}   // Sequencer
};

// Version string
static const char* kVersion = "1.0.0-wasm";

// Audio callback for demo (thread-safe)
static void audioCallback(const float* const* input, float* const* output,
                          int numInputChannels, int numOutputChannels, int numSamples) {
    // Mark that audio callback is active
    gAudioCallbackActive.store(true);
    
    // Simple demo: generate a sine wave with frequency controlled by first knob
    static float phase = 0.0f;
    float frequency = 440.0f;
    
    // Thread-safe read of knob value
    if (gInitialized && !gKnobs.empty()) {
        frequency = 100.0f + gKnobs[0]->getValue() * 800.0f;  // 100-900 Hz
    }
    
    float sampleRate = gAudioBackend ? gAudioBackend->getSampleRate() : 44100;
    float phaseInc = 2.0f * 3.14159265f * frequency / sampleRate;
    
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

// Helper function to get panel name
static const char* getPanelName(int panelIndex) {
    static const char* panelNames[] = {"Mixer", "Effects", "Sequencer"};
    if (panelIndex >= 0 && panelIndex < PANEL_COUNT) {
        return panelNames[panelIndex];
    }
    return "Unknown";
}

// Helper function to log panel status
static void logPanelStatus(int panelIndex, const char* action) {
    if (panelIndex >= 0 && panelIndex < PANEL_COUNT) {
        printf("DEBUG [Panel:%s] %s - Loaded:%s Running:%s Frames:%d\n",
               getPanelName(panelIndex), action,
               gPanelStatus[panelIndex].loaded ? "YES" : "NO",
               gPanelStatus[panelIndex].running ? "YES" : "NO",
               gPanelStatus[panelIndex].frameCount);
    }
}

// Helper function to mark panel as loaded
static void markPanelLoaded(int panelIndex) {
    if (panelIndex >= 0 && panelIndex < PANEL_COUNT) {
        gPanelStatus[panelIndex].loaded = true;
        gPanelStatus[panelIndex].running = false;
        gPanelStatus[panelIndex].frameCount = 0;
        logPanelStatus(panelIndex, "LOADED");
    }
}

// Helper function to mark panel as running
static void markPanelRunning(int panelIndex) {
    if (panelIndex >= 0 && panelIndex < PANEL_COUNT) {
        if (!gPanelStatus[panelIndex].running) {
            gPanelStatus[panelIndex].running = true;
            logPanelStatus(panelIndex, "STARTED");
        }
    }
}

extern "C" {

EMSCRIPTEN_KEEPALIVE int bespoke_init(int width, int height, int sampleRate, int bufferSize) {
    printf("BespokeSynth WASM: Initializing (%dx%d, %dHz, %d samples)\n", 
           width, height, sampleRate, bufferSize);
    
    if (gInitState != InitState::NotStarted) {
        printf("BespokeSynth WASM: Already initialized or in progress (state=%d)\n", static_cast<int>(gInitState));
        return gInitState == InitState::FullyInitialized ? 0 : 1;
    }
    
    gWidth = width;
    gHeight = height;
    gInitState = InitState::WebGPURequested;
    
    // Initialize WebGPU context (asynchronous)
    gContext = std::make_unique<WebGPUContext>();
    printf("WasmBridge: starting async WebGPU initialization (selector=#canvas)\n");

    bool started = gContext->initializeAsync("#canvas", [] (bool success) {
        if (!success) {
            printf("BespokeSynth WASM: Failed to initialize WebGPU\n");
            printf("WasmBridge: notifying JS of init failure (-1)\n");
            gInitState = InitState::Failed;
            gInitErrorMessage = "WebGPU initialization failed";
            // Notify JS that initialization failed
            emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-1);");
            return;
        }

        // WebGPU initialized successfully
        printf("WasmBridge: WebGPU context ready, proceeding with remaining initialization\n");
        gInitState = InitState::WebGPUReady;
        
        // Continue remaining initialization on success
        gContext->resize(gWidth, gHeight);

        // Initialize renderer
        printf("WasmBridge: Initializing renderer...\n");
        gRenderer = std::make_unique<WebGPURenderer>(*gContext);
        if (!gRenderer->initialize()) {
            printf("BespokeSynth WASM: Failed to initialize renderer\n");
            printf("WasmBridge: notifying JS of init failure (-2)\n");
            gInitState = InitState::Failed;
            gInitErrorMessage = "Renderer initialization failed";
            emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-2);");
            return;
        }
        
        printf("WasmBridge: Renderer initialized successfully\n");
        gInitState = InitState::RendererReady;

        // Initialize audio backend
        printf("WasmBridge: Initializing audio backend...\n");
        gAudioBackend = std::make_unique<SDL2AudioBackend>();
        if (!gAudioBackend->initialize(44100, 512, 2, 0)) {
            printf("BespokeSynth WASM: Failed to initialize audio\n");
            printf("WasmBridge: notifying JS of init failure (-3)\n");
            gInitState = InitState::Failed;
            gInitErrorMessage = "Audio backend initialization failed";
            emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-3);");
            return;
        }

        printf("WasmBridge: Audio backend initialized successfully\n");
        gInitState = InitState::AudioReady;
        
        // Set audio callback
        gAudioBackend->setCallback(audioCallback);

        // Create some demo knobs
        printf("WasmBridge: Creating demo controls...\n");
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

        // Mark all panels as loaded
        printf("\n=== DEBUG: Panel Initialization ===\n");
        for (int i = 0; i < PANEL_COUNT; i++) {
            markPanelLoaded(i);
        }
        printf("=== Panel Initialization Complete ===\n\n");

        gInitState = InitState::FullyInitialized;
        gInitialized = true;
        printf("BespokeSynth WASM: Initialization complete - all subsystems ready\n");
        printf("WasmBridge: notifying JS of init complete (0)\n");

        // Notify JS that initialization finished successfully
        emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(0);");
    });

    if (!started) {
        printf("BespokeSynth WASM: Failed to start WebGPU initialization\n");
        gInitState = InitState::Failed;
        gInitErrorMessage = "Failed to start async WebGPU initialization";
        return -1;
    }

    // Indicate initialization started asynchronously
    return 1;
}

EMSCRIPTEN_KEEPALIVE void bespoke_process_events(void) {
    // Process pending WebGPU events to allow async callbacks to fire
    if (gContext) {
        gContext->processEvents();
    }
}

EMSCRIPTEN_KEEPALIVE void bespoke_shutdown(void) {
    printf("BespokeSynth WASM: Shutting down\n");
    
    // Clear controls first
    gKnobs.clear();
    
    // Stop and cleanup audio
    if (gAudioBackend) {
        gAudioBackend->stop();
        gAudioBackend->shutdown();
        gAudioBackend.reset();
    }
    
    // Wait for audio callback to complete if it's running
    int waitCount = 0;
    while (gAudioCallbackActive.load() && waitCount < 100) {
        emscripten_sleep(10);
        waitCount++;
    }
    
    // Cleanup renderer and context
    gRenderer.reset();
    gContext.reset();
    
    // Reset state
    gInitialized = false;
    gInitState = InitState::NotStarted;
    gInitErrorMessage.clear();
    
    printf("BespokeSynth WASM: Shutdown complete\n");
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
    // During initialization, process events to allow WebGPU callbacks
    if (gInitState != InitState::FullyInitialized) {
        if (gContext) {
            gContext->processEvents();
        }
        return;
    }
    
    // Double-check all subsystems are ready
    if (!gInitialized || !gRenderer || !gContext) {
        printf("BespokeSynth WASM: Render called but not fully initialized\n");
        return;
    }
    
    // Verify WebGPU context is still valid
    if (!gContext->isInitialized()) {
        printf("BespokeSynth WASM: WebGPU context lost, cannot render\n");
        return;
    }
    
    gTime += 0.016f; // Approximate 60fps
    
    // Mark current panel as running and update frame count
    if (gCurrentPanel >= 0 && gCurrentPanel < PANEL_COUNT) {
        markPanelRunning(gCurrentPanel);
        gPanelStatus[gCurrentPanel].frameCount++;
        gPanelStatus[gCurrentPanel].lastUpdateTime = gTime;
        
        // Log panel status every 300 frames (~5 seconds at 60fps)
        if (gPanelStatus[gCurrentPanel].frameCount % 300 == 0) {
            printf("DEBUG [Panel:%s] Running - Frames:%d Time:%.1fs\n",
                   getPanelName(gCurrentPanel),
                   gPanelStatus[gCurrentPanel].frameCount,
                   gTime);
        }
    }
    
    gRenderer->beginFrame(gWidth, gHeight, 1.0f, gTime);
    
    // Clear background
    gRenderer->fillColor(Color(0.12f, 0.12f, 0.14f, 1.0f));
    gRenderer->rect(0, 0, static_cast<float>(gWidth), static_cast<float>(gHeight));
    gRenderer->fill();
    
    // Draw title
    gRenderer->fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
    gRenderer->fontSize(24.0f);
    gRenderer->text(20, 40, "BespokeSynth WASM - WebGPU Demo");
    
    // Draw panel tabs
    float tabY = 70.0f;
    float tabHeight = 35.0f;
    float tabWidth = 150.0f;
    float tabSpacing = 5.0f;
    
    const char* panelNames[] = {"Mixer", "Effects", "Sequencer"};
    
    for (int i = 0; i < PANEL_COUNT; i++) {
        float tabX = 20.0f + i * (tabWidth + tabSpacing);
        
        // Draw tab background
        if (i == gCurrentPanel) {
            gRenderer->fillColor(Color(0.25f, 0.25f, 0.28f, 1.0f));
        } else {
            gRenderer->fillColor(Color(0.18f, 0.18f, 0.2f, 1.0f));
        }
        gRenderer->roundedRect(tabX, tabY, tabWidth, tabHeight, 5.0f);
        gRenderer->fill();
        
        // Draw tab border (green if loaded and running, blue if active, default otherwise)
        if (gPanelStatus[i].loaded && gPanelStatus[i].running) {
            gRenderer->strokeColor(Color(0.3f, 0.8f, 0.4f, 1.0f));  // Green for running
        } else if (i == gCurrentPanel) {
            gRenderer->strokeColor(Color(0.4f, 0.7f, 0.9f, 1.0f));  // Blue for active
        } else {
            gRenderer->strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));  // Gray for inactive
        }
        gRenderer->strokeWidth(2.0f);
        gRenderer->roundedRect(tabX, tabY, tabWidth, tabHeight, 5.0f);
        gRenderer->stroke();
        
        // Draw tab label
        if (i == gCurrentPanel) {
            gRenderer->fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
        } else {
            gRenderer->fillColor(Color(0.6f, 0.6f, 0.65f, 1.0f));
        }
        gRenderer->fontSize(14.0f);
        gRenderer->text(tabX + 15, tabY + 22, panelNames[i]);
    }
    
    // Draw knobs in a row (common across all panels)
    float knobSize = 80.0f;
    float startX = 100.0f;
    float startY = 130.0f;
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
    
    // Draw panel based on selection
    float panelX = 50.0f;
    float panelY = 260.0f;
    float panelW = static_cast<float>(gWidth) - 100.0f;
    float panelH = 220.0f;
    
    // Panel background
    gRenderer->fillColor(Color(0.18f, 0.18f, 0.2f, 1.0f));
    gRenderer->roundedRect(panelX, panelY, panelW, panelH, 8.0f);
    gRenderer->fill();
    
    gRenderer->strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
    gRenderer->strokeWidth(1.0f);
    gRenderer->roundedRect(panelX, panelY, panelW, panelH, 8.0f);
    gRenderer->stroke();
    
    // Panel title
    gRenderer->fillColor(Color(0.8f, 0.8f, 0.85f, 1.0f));
    gRenderer->fontSize(16.0f);
    gRenderer->text(panelX + 15, panelY + 25, panelNames[gCurrentPanel]);
    
    // Render panel-specific content
    switch (gCurrentPanel) {
        case PANEL_MIXER: {
            // Draw mixer controls - sliders and VU meters
            float sliderX = panelX + 30;
            float sliderY = panelY + 50;
            
            gRenderer->fillColor(Color(0.5f, 0.5f, 0.55f, 1.0f));
            gRenderer->fontSize(12.0f);
            gRenderer->text(sliderX, sliderY - 10, "Channel 1");
            gRenderer->drawSlider(sliderX, sliderY, 200, 20, 0.6f,
                Color(0.25f, 0.25f, 0.28f, 1.0f),
                Color(0.4f, 0.7f, 0.5f, 1.0f));
            
            gRenderer->text(sliderX, sliderY + 40, "Channel 2");
            gRenderer->drawSlider(sliderX, sliderY + 50, 200, 20, 0.3f,
                Color(0.25f, 0.25f, 0.28f, 1.0f),
                Color(0.5f, 0.6f, 0.9f, 1.0f));
            
            gRenderer->text(sliderX, sliderY + 90, "Master");
            gRenderer->drawSlider(sliderX, sliderY + 100, 200, 20, 0.8f,
                Color(0.25f, 0.25f, 0.28f, 1.0f),
                Color(0.9f, 0.5f, 0.3f, 1.0f));
            
            // Draw VU meters
            float vuX = panelX + panelW - 120;
            float vuY = panelY + 40;
            
            float audioLevel = gAudioBackend ? gAudioBackend->getOutputLevel() : 0.0f;
            
            gRenderer->fillColor(Color(0.5f, 0.5f, 0.55f, 1.0f));
            gRenderer->text(vuX, vuY - 10, "L");
            gRenderer->drawVUMeter(vuX, vuY, 20, 160, audioLevel,
                Color(0.2f, 0.8f, 0.3f, 1.0f),
                Color(1.0f, 0.2f, 0.1f, 1.0f));
            
            gRenderer->text(vuX + 40, vuY - 10, "R");
            gRenderer->drawVUMeter(vuX + 40, vuY, 20, 160, audioLevel * 0.9f,
                Color(0.2f, 0.8f, 0.3f, 1.0f),
                Color(1.0f, 0.2f, 0.1f, 1.0f));
            break;
        }
        
        case PANEL_EFFECTS: {
            // Draw effects controls
            float effectX = panelX + 30;
            float effectY = panelY + 50;
            
            gRenderer->fillColor(Color(0.5f, 0.5f, 0.55f, 1.0f));
            gRenderer->fontSize(12.0f);
            gRenderer->text(effectX, effectY - 10, "Reverb Mix");
            gRenderer->drawSlider(effectX, effectY, 250, 20, 0.4f,
                Color(0.25f, 0.25f, 0.28f, 1.0f),
                Color(0.6f, 0.3f, 0.8f, 1.0f));
            
            gRenderer->text(effectX, effectY + 40, "Delay Time");
            gRenderer->drawSlider(effectX, effectY + 50, 250, 20, 0.5f,
                Color(0.25f, 0.25f, 0.28f, 1.0f),
                Color(0.8f, 0.6f, 0.3f, 1.0f));
            
            gRenderer->text(effectX, effectY + 90, "Chorus Depth");
            gRenderer->drawSlider(effectX, effectY + 100, 250, 20, 0.7f,
                Color(0.25f, 0.25f, 0.28f, 1.0f),
                Color(0.3f, 0.8f, 0.8f, 1.0f));
            
            gRenderer->text(effectX, effectY + 140, "Distortion");
            gRenderer->drawSlider(effectX, effectY + 150, 250, 20, 0.2f,
                Color(0.25f, 0.25f, 0.28f, 1.0f),
                Color(0.9f, 0.3f, 0.3f, 1.0f));
            
            // Draw effect visualizer
            float vizX = panelX + panelW - 200;
            float vizY = panelY + 50;
            float vizW = 180;
            float vizH = 150;
            
            gRenderer->fillColor(Color(0.1f, 0.1f, 0.12f, 1.0f));
            gRenderer->rect(vizX, vizY, vizW, vizH);
            gRenderer->fill();
            
            gRenderer->strokeColor(Color(0.3f, 0.6f, 0.8f, 0.8f));
            gRenderer->strokeWidth(2.0f);
            
            // Draw waveform visualization
            for (int i = 0; i < 10; i++) {
                float x1 = vizX + i * vizW / 10;
                float x2 = vizX + (i + 1) * vizW / 10;
                float y1 = vizY + vizH / 2 + sinf(i * 0.5f) * 30;
                float y2 = vizY + vizH / 2 + sinf((i + 1) * 0.5f) * 30;
                gRenderer->line(x1, y1, x2, y2);
            }
            break;
        }
        
        case PANEL_SEQUENCER: {
            // Draw sequencer controls
            float seqX = panelX + 30;
            float seqY = panelY + 50;
            
            gRenderer->fillColor(Color(0.5f, 0.5f, 0.55f, 1.0f));
            gRenderer->fontSize(12.0f);
            gRenderer->text(seqX, seqY - 10, "BPM");
            gRenderer->drawSlider(seqX, seqY, 150, 20, 0.6f,
                Color(0.25f, 0.25f, 0.28f, 1.0f),
                Color(0.5f, 0.8f, 0.4f, 1.0f));
            
            gRenderer->text(seqX + 200, seqY - 10, "Swing");
            gRenderer->drawSlider(seqX + 200, seqY, 150, 20, 0.5f,
                Color(0.25f, 0.25f, 0.28f, 1.0f),
                Color(0.8f, 0.7f, 0.4f, 1.0f));
            
            // Draw step sequencer grid
            float gridX = seqX;
            float gridY = seqY + 50;
            
            // Sequencer grid constants
            const float STEP_WIDTH = 35.0f;
            const float STEP_HEIGHT = 30.0f;
            const int NUM_STEPS = 16;
            const int NUM_ROWS = 4;
            const int STEP_PATTERN_INTERVAL = 3;  // Pattern interval for demo
            
            gRenderer->text(gridX, gridY - 10, "Step Sequencer (16 steps x 4 notes)");
            
            for (int row = 0; row < NUM_ROWS; row++) {
                for (int step = 0; step < NUM_STEPS; step++) {
                    float sx = gridX + step * STEP_WIDTH;
                    float sy = gridY + row * STEP_HEIGHT;
                    
                    // Alternate pattern for demo
                    bool active = (step + row) % STEP_PATTERN_INTERVAL == 0;
                    
                    if (active) {
                        gRenderer->fillColor(Color(0.4f, 0.7f, 0.5f, 1.0f));
                    } else {
                        gRenderer->fillColor(Color(0.15f, 0.15f, 0.17f, 1.0f));
                    }
                    
                    gRenderer->rect(sx, sy, STEP_WIDTH - 2, STEP_HEIGHT - 2);
                    gRenderer->fill();
                    
                    gRenderer->strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
                    gRenderer->strokeWidth(1.0f);
                    gRenderer->rect(sx, sy, STEP_WIDTH - 2, STEP_HEIGHT - 2);
                    gRenderer->stroke();
                }
            }
            break;
        }
    }
    
    // Draw status
    gRenderer->fillColor(Color(0.6f, 0.6f, 0.65f, 1.0f));
    gRenderer->fontSize(12.0f);
    
    char statusText[256];
    snprintf(statusText, sizeof(statusText), 
             "Sample Rate: %d Hz | Buffer: %d | Audio: %s | Panel: %s",
             bespoke_get_sample_rate(),
             bespoke_get_buffer_size(),
             (gAudioBackend && gAudioBackend->isRunning()) ? "Running" : "Stopped",
             panelNames[gCurrentPanel]);
    
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
    
    // Check panel tab clicks
    float tabY = 70.0f;
    float tabHeight = 35.0f;
    float tabWidth = 150.0f;
    float tabSpacing = 5.0f;
    
    if (y >= tabY && y <= tabY + tabHeight) {
        for (int i = 0; i < PANEL_COUNT; i++) {
            float tabX = 20.0f + i * (tabWidth + tabSpacing);
            if (x >= tabX && x <= tabX + tabWidth) {
                int prevPanel = gCurrentPanel;
                gCurrentPanel = i;
                printf("DEBUG [Panel Switch] From:%s To:%s\n", 
                       getPanelName(prevPanel), getPanelName(i));
                logPanelStatus(i, "ACTIVATED");
                return;
            }
        }
    }
    
    // Check knob hit testing
    float knobSize = 80.0f;
    float startX = 100.0f;
    float startY = 130.0f;  // Updated to match render
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
    float startY = 130.0f;  // Updated to match render
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

EMSCRIPTEN_KEEPALIVE void bespoke_set_panel(int panelIndex) {
    if (panelIndex >= 0 && panelIndex < PANEL_COUNT) {
        int prevPanel = gCurrentPanel;
        gCurrentPanel = panelIndex;
        printf("DEBUG [API] Panel switch via bespoke_set_panel: From:%s To:%s\n",
               getPanelName(prevPanel), getPanelName(panelIndex));
        logPanelStatus(panelIndex, "ACTIVATED");
    }
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_panel(void) {
    return gCurrentPanel;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_panel_count(void) {
    return PANEL_COUNT;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_panel_name(int panelIndex) {
    return getPanelName(panelIndex);
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_panel_loaded(int panelIndex) {
    if (panelIndex >= 0 && panelIndex < PANEL_COUNT) {
        return gPanelStatus[panelIndex].loaded ? 1 : 0;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_panel_running(int panelIndex) {
    if (panelIndex >= 0 && panelIndex < PANEL_COUNT) {
        return gPanelStatus[panelIndex].running ? 1 : 0;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_panel_frame_count(int panelIndex) {
    if (panelIndex >= 0 && panelIndex < PANEL_COUNT) {
        return gPanelStatus[panelIndex].frameCount;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_log_all_panels_status(void) {
    printf("\n=== DEBUG: All Panels Status ===\n");
    for (int i = 0; i < PANEL_COUNT; i++) {
        logPanelStatus(i, "STATUS CHECK");
    }
    printf("=== End Panel Status ===\n\n");
}

// New initialization state query functions
EMSCRIPTEN_KEEPALIVE int bespoke_get_init_state(void) {
    return static_cast<int>(gInitState);
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_init_error(void) {
    return gInitErrorMessage.c_str();
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_fully_initialized(void) {
    return (gInitState == InitState::FullyInitialized) ? 1 : 0;
}

} // extern "C"
