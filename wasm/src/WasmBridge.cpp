/**
 * BespokeSynth WASM - Bridge Implementation
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

<<<<<<< HEAD
#include "WasmBridge.h"
#include "IRenderer.h"
#include "WebGPUContext.h"
#include "WebGPURenderer.h"
#include "WebGL2Renderer.h"
=======
#include "BespokeWasm/WasmBridge.h"
#include "Renderer2D.h"
#include "WebGPUContext.h"
#include "WebGL2Context.h"
>>>>>>> origin/main
#include "SDL2AudioBackend.h"
#include "ModuleCanvas.h"
#include "Knob.h"
#include "BespokeWasm/Theme.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <emscripten.h>  // Required for emscripten_run_script

// Key code constants
static const int KEY_SHIFT = 16;
static const int KEY_SPACE = 32;
static const int KEY_TAB = 9;

using namespace bespoke::wasm;

namespace bespoke {
namespace wasm {
bool gBespokePendingScreenshotCapture = false;
} // namespace wasm
} // namespace bespoke

// Initialization state tracking
// These values are exposed to JavaScript via bespoke_get_init_state()
enum class InitState
{
   NotStarted = 0,
   WebGPURequested = 1, // WebGPU async init started (instance + surface + adapter)
   WebGPUReady = 2, // WebGPU adapter and device acquired
   RendererReady = 3, // Renderer pipelines compiled (both backends)
   AudioReady = 4, // Audio backend initialized
   FullyInitialized = 5, // All subsystems ready
   WebGL2Requested = 6, // WebGL2 context creation started
   WebGL2Ready = 7, // WebGL2 context acquired and extensions queried
   Failed = -1
};

// Helper to report progress to JavaScript
static void reportInitProgress(const char* step, const char* detail)
{
   printf("[InitProgress] %s: %s\n", step, detail);

   // Also notify JS via a callback if available
   char script[512];
   snprintf(script, sizeof(script),
            "if (window.__bespoke_on_init_progress) {"
            "  window.__bespoke_on_init_progress('%s', '%s');"
            "}",
            step, detail);
   emscripten_run_script(script);
}

// Global state
static RendererBackendType gRendererBackend = RendererBackendType::WebGPU;
static bool gRenderTestMode = false;
static std::vector<uint8_t> gScreenshotPixels;
static int gScreenshotWidth = 0;
static int gScreenshotHeight = 0;
static WebGLDebugMode gWebGLDebugMode = WebGLDebugMode::Normal;
static std::unique_ptr<WebGPUContext> gContext;
<<<<<<< HEAD
static std::unique_ptr<bespoke::wasm::IRenderer> gRenderer;
=======
static std::unique_ptr<WebGL2Context> gWebGL2Context;
static std::unique_ptr<Renderer2D> gRenderer;
>>>>>>> origin/main
static std::unique_ptr<SDL2AudioBackend> gAudioBackend;

// Active renderer backend selection (0=Auto, 1=WebGPU, 2=WebGL2)
static int gRendererBackendPref = 0;

// Demo controls
static std::vector<std::unique_ptr<Knob>> gKnobs;

// Modular canvas
static std::unique_ptr<bespoke::wasm::ModuleCanvas> gCanvas;
static int gOscillatorModuleId = -1;
static int gGainModuleId = -1;

// View modes: 0 = modular canvas (default), 1 = legacy demo panels
static int gViewMode = 0;
static bool gFontTestVisible = false;

static int gWidth = 800;
static int gHeight = 600;
static bool gInitialized = false;
static float gTime = 0.0f;

// Panel selection
enum PanelType
{
   PANEL_MIXER = 0,
   PANEL_EFFECTS,
   PANEL_SEQUENCER,
   PANEL_COUNT
};
static int gCurrentPanel = PANEL_MIXER;

// Panel status tracking
struct PanelStatus
{
   bool loaded;
   bool running;
   int frameCount;
   float lastUpdateTime;
};
static PanelStatus gPanelStatus[PANEL_COUNT] = {
   { false, false, 0, 0.0f }, // Mixer
   { false, false, 0, 0.0f }, // Effects
   { false, false, 0, 0.0f } // Sequencer
};

// Version string
static const char* kVersion = "1.0.0-wasm";

// Initialization state
static InitState gInitState = InitState::NotStarted;
static std::string gInitErrorMessage;
static std::atomic<bool> gAudioCallbackActive{false};

// ---- Control inspection cache ----
// Updated each frame by renderDemoPanels() so that bespoke_get_control_info()
// always reflects current screen positions.
struct ControlInfoEntry {
   int   id;
   char  type[16];
   char  label[64];
   float value;
   float min;
   float max;
   char  unit[16];
   float x;
   float y;
   float size;
};
static std::vector<ControlInfoEntry> gControlInfoCache;

// Runtime theme – one global instance used by bespoke_set_theme_color().
// Defines the instance declared 'extern' in BespokeWasm/Theme.h, so it must
// live in the bespoke::wasm namespace (otherwise an unqualified reference is
// ambiguous between this global and the namespaced declaration).
namespace bespoke { namespace wasm { RuntimeTheme gRuntimeTheme; } }

// RuntimeTheme::resolve() – returns the override if active, else UITheme default.
bespoke::wasm::Color bespoke::wasm::RuntimeTheme::resolve(bespoke::wasm::ThemeColorId id) const
{
   using namespace bespoke::wasm::UITheme;
   int i = static_cast<int>(id);
   if (i >= 0 && i < static_cast<int>(bespoke::wasm::ThemeColorId::Count) && active[i]) {
      return colors[i];
   }
   switch (id) {
      case bespoke::wasm::ThemeColorId::BgDark:        return kBgDark;
      case bespoke::wasm::ThemeColorId::BgPanel:       return kBgPanel;
      case bespoke::wasm::ThemeColorId::BgTrack:       return kBgTrack;
      case bespoke::wasm::ThemeColorId::BgElevated:    return kBgElevated;
      case bespoke::wasm::ThemeColorId::TextPrimary:   return kTextPrimary;
      case bespoke::wasm::ThemeColorId::TextSecondary: return kTextSecondary;
      case bespoke::wasm::ThemeColorId::TextValue:     return kTextValue;
      case bespoke::wasm::ThemeColorId::AccentCyan:    return kAccentCyan;
      case bespoke::wasm::ThemeColorId::AccentMagenta: return kAccentMagenta;
      case bespoke::wasm::ThemeColorId::AccentAmber:   return kAccentAmber;
      case bespoke::wasm::ThemeColorId::AccentGreen:   return kAccentGreen;
      case bespoke::wasm::ThemeColorId::KnobBg:        return kKnobBg;
      case bespoke::wasm::ThemeColorId::KnobFg:        return kKnobFg;
      case bespoke::wasm::ThemeColorId::KnobIndicator: return kKnobIndicator;
      case bespoke::wasm::ThemeColorId::KnobRim:       return kKnobRim;
      default:                                          return kTextPrimary;
   }
}

// Audio callback — generates oscillator tone gated by transport playing state
static void audioCallback(const float* const* input, float* const* output,
                          int numInputChannels, int numOutputChannels, int numSamples)
{
   gAudioCallbackActive.store(true);

   if (numOutputChannels <= 0 || numSamples <= 0 || !output)
   {
      gAudioCallbackActive.store(false);
      return;
   }

   const int maxChannels = 2;
   int channels = (numOutputChannels > maxChannels) ? maxChannels : numOutputChannels;

   // Silence all channels, then fill if playing
   for (int ch = 0; ch < channels; ch++)
      if (output[ch])
         memset(output[ch], 0, numSamples * sizeof(float));

   // Only generate audio when transport is playing
   bool isPlaying = false;
   if (gCanvas && gCanvas->getTransport())
      isPlaying = gCanvas->getTransport()->isPlaying();

   if (!isPlaying)
   {
      gAudioCallbackActive.store(false);
      return;
   }

   // Frequency from oscillator module, fallback to first knob
   float frequency = 440.0f;
   if (gCanvas && gOscillatorModuleId >= 0)
   {
      auto* osc = gCanvas->getModule(gOscillatorModuleId);
      if (osc)
         frequency = osc->getControlValue("frequency");
   }
   else if (gInitialized && !gKnobs.empty())
   {
      frequency = gKnobs[0]->getValue();
   }

   // Gain from gain module, fallback to 1.0
   float gain = 1.0f;
   if (gCanvas && gGainModuleId >= 0)
   {
      auto* gainMod = gCanvas->getModule(gGainModuleId);
      if (gainMod)
         gain = gainMod->getControlValue("gain");
   }

   float sampleRate = gAudioBackend ? gAudioBackend->getSampleRate() : 44100;
   float phaseInc = 2.0f * 3.14159265f * frequency / sampleRate;
   static float phase = 0.0f;

   for (int i = 0; i < numSamples; i++)
   {
      float sample = sinf(phase) * 0.3f * gain;
      phase += phaseInc;
      if (phase > 2.0f * 3.14159265f)
         phase -= 2.0f * 3.14159265f;

      for (int ch = 0; ch < channels; ch++)
         if (output[ch])
            output[ch][i] = sample;
   }

   gAudioCallbackActive.store(false);
}

// Helper function to get panel name
static const char* getPanelName(int panelIndex)
{
   static const char* panelNames[] = { "Mixer", "Effects", "Sequencer" };
   if (panelIndex >= 0 && panelIndex < PANEL_COUNT)
   {
      return panelNames[panelIndex];
   }
   return "Unknown";
}

// Helper function to log panel status
static void logPanelStatus(int panelIndex, const char* action)
{
   if (panelIndex >= 0 && panelIndex < PANEL_COUNT)
   {
      printf("DEBUG [Panel:%s] %s - Loaded:%s Running:%s Frames:%d\n",
             getPanelName(panelIndex), action,
             gPanelStatus[panelIndex].loaded ? "YES" : "NO",
             gPanelStatus[panelIndex].running ? "YES" : "NO",
             gPanelStatus[panelIndex].frameCount);
   }
}

// Helper function to mark panel as loaded
static void markPanelLoaded(int panelIndex)
{
   if (panelIndex >= 0 && panelIndex < PANEL_COUNT)
   {
      gPanelStatus[panelIndex].loaded = true;
      gPanelStatus[panelIndex].running = false;
      gPanelStatus[panelIndex].frameCount = 0;
      logPanelStatus(panelIndex, "LOADED");
   }
}

// Helper function to mark panel as running
static void markPanelRunning(int panelIndex)
{
   if (panelIndex >= 0 && panelIndex < PANEL_COUNT)
   {
      if (!gPanelStatus[panelIndex].running)
      {
         gPanelStatus[panelIndex].running = true;
         logPanelStatus(panelIndex, "STARTED");
      }
   }
}

static bool initializeAudioAndControls(int sampleRate, int bufferSize)
{
   printf("WasmBridge: Initializing audio backend...\n");
   reportInitProgress("audio_init", "Opening audio device...");
   gAudioBackend = std::make_unique<SDL2AudioBackend>();
   if (!gAudioBackend->initialize(sampleRate, bufferSize, 2, 0))
   {
      printf("BespokeSynth WASM: Failed to initialize audio\n");
      reportInitProgress("audio_failed", "Audio device initialization failed");
      gInitState = InitState::Failed;
      gInitErrorMessage = "Audio backend initialization failed";
      return false;
   }

   printf("WasmBridge: Audio backend initialized successfully\n");
   reportInitProgress("audio_ready", "Audio device opened successfully");
   gInitState = InitState::AudioReady;
   gAudioBackend->setCallback(audioCallback);
   reportInitProgress("audio_ready", "Audio ready - click play to start");

   printf("WasmBridge: Creating demo controls...\n");
   reportInitProgress("controls_init", "Creating UI controls...");
   gKnobs.clear();

   auto knob1 = std::make_unique<Knob>("Frequency", 0.5f);
   knob1->setRange(100.0f, 900.0f);
   knob1->setUnit("Hz");
   knob1->setDisplayPrecision(0);
   knob1->setStyle(KnobStyle::Classic);
   knob1->setColors(
      Color(0.25f, 0.25f, 0.28f, 1.0f),
      Color(0.7f, 0.7f, 0.75f, 1.0f),
      Color(0.4f, 0.8f, 0.5f, 1.0f));
   gKnobs.push_back(std::move(knob1));

   auto knob2 = std::make_unique<Knob>("Volume", 0.7f);
   knob2->setRange(0.0f, 1.0f);
   knob2->setUnit("%");
   knob2->setDisplayPrecision(0);
   knob2->setStyle(KnobStyle::Modern);
   knob2->setColors(
      Color(0.2f, 0.2f, 0.22f, 1.0f),
      Color(0.6f, 0.6f, 0.65f, 1.0f),
      Color(0.3f, 0.7f, 0.9f, 1.0f));
   gKnobs.push_back(std::move(knob2));

   auto knob3 = std::make_unique<Knob>("Filter", 0.3f);
   knob3->setRange(20.0f, 20000.0f);
   knob3->setUnit("Hz");
   knob3->setDisplayPrecision(0);
   knob3->setStyle(KnobStyle::LED);
   knob3->setColors(
      Color(0.15f, 0.15f, 0.18f, 1.0f),
      Color(0.5f, 0.5f, 0.55f, 1.0f),
      Color(0.9f, 0.4f, 0.2f, 1.0f));
   gKnobs.push_back(std::move(knob3));

   auto knob4 = std::make_unique<Knob>("Pan", 0.5f);
   knob4->setRange(-1.0f, 1.0f);
   knob4->setUnit("");
   knob4->setDisplayPrecision(2);
   knob4->setBipolar(true);
   knob4->setStyle(KnobStyle::Vintage);
   gKnobs.push_back(std::move(knob4));

   for (int i = 0; i < PANEL_COUNT; i++)
      markPanelLoaded(i);

   printf("WasmBridge: Creating modular canvas...\n");
   gCanvas = std::make_unique<bespoke::wasm::ModuleCanvas>();

   const int oscId = gCanvas->createModule("oscillator", 100, 150);
   const int gainId = gCanvas->createModule("gain", 320, 160);
   const int outputId = gCanvas->createModule("output", 500, 170);
   gOscillatorModuleId = oscId;
   gGainModuleId = gainId;

   if (oscId > 0 && gainId > 0 && outputId > 0)
   {
      gCanvas->connectModules(oscId, 0, gainId, 0);
      gCanvas->connectModules(gainId, 0, outputId, 0);

      // Seed canvas module values from demo knobs
      if (auto* osc = gCanvas->getModule(oscId))
      {
         osc->setControlValue("frequency", gKnobs[0]->getValue());
         osc->setControlValue("volume", gKnobs[1]->getValue());
      }
      if (auto* gain = gCanvas->getModule(gainId))
         gain->setControlValue("gain", gKnobs[1]->getValue());
   }

   gInitState = InitState::FullyInitialized;
   gInitialized = true;
   reportInitProgress("init_complete", "All subsystems ready");
   printf("BespokeSynth WASM: Initialization complete - all subsystems ready\n");
   return true;
}

static bool initializeWebGL2Renderer(int sampleRate, int bufferSize)
{
   gInitState = InitState::WebGL2Requested;
   reportInitProgress("webgl_requested", "Creating WebGL2 context on #canvas");

   if (!WebGL2Context::probeSupport("#canvas"))
   {
      gInitState = InitState::Failed;
      gInitErrorMessage = WebGL2Context::getLastError();
      if (gInitErrorMessage.empty())
         gInitErrorMessage = "WebGL2 is not supported in this browser";
      reportInitProgress("webgl_failed", gInitErrorMessage.c_str());
      return false;
   }

   gWebGL2Context = std::make_unique<WebGL2Context>();
   if (!gWebGL2Context->initialize("#canvas"))
   {
      gInitState = InitState::Failed;
      gInitErrorMessage = WebGL2Context::getLastError();
      if (gInitErrorMessage.empty())
         gInitErrorMessage = "WebGL2 context creation failed";
      reportInitProgress("webgl_failed", gInitErrorMessage.c_str());
      return false;
   }

   gWebGL2Context->resize(gWidth, gHeight);
   gInitState = InitState::WebGL2Ready;

   const auto& caps = gWebGL2Context->getCapabilities();
   char readyDetail[256];
   snprintf(readyDetail, sizeof(readyDetail),
            "WebGL2 context ready (maxTexture=%d, VAO=%s, instancing=%s)",
            caps.maxTextureSize,
            caps.vertexArrayObject ? "yes" : "no",
            caps.instancedArrays ? "yes" : "no");
   reportInitProgress("webgl_ready", readyDetail);

   reportInitProgress("renderer_init", "Creating WebGL2 shader programs...");
   auto glRenderer = createWebGL2Renderer(*gWebGL2Context);
   glRenderer->setDebugMode(gWebGLDebugMode);
   if (!glRenderer->initialize())
   {
      gInitState = InitState::Failed;
      gInitErrorMessage = "WebGL2 renderer initialization failed — shader compilation error";
      reportInitProgress("renderer_failed", gInitErrorMessage.c_str());
      return false;
   }

   gRenderer = std::move(glRenderer);
   gInitState = InitState::RendererReady;
   reportInitProgress("renderer_ready", "WebGL2 renderer ready");
   return initializeAudioAndControls(sampleRate, bufferSize);
}

extern "C" {

EMSCRIPTEN_KEEPALIVE int bespoke_init(int width, int height, int sampleRate, int bufferSize)
{
   printf("BespokeSynth WASM: Initializing (%dx%d, %dHz, %d samples, backend=%d)\n",
          width, height, sampleRate, bufferSize, static_cast<int>(gRendererBackend));
   reportInitProgress("init_start", "Beginning initialization");

   if (gInitState != InitState::NotStarted)
   {
      printf("BespokeSynth WASM: Already initialized or in progress (state=%d)\n", static_cast<int>(gInitState));
      return gInitState == InitState::FullyInitialized ? 0 : 1;
   }

   gWidth = width;
   gHeight = height;

   if (gRendererBackend == RendererBackendType::WebGL2)
   {
      if (!initializeWebGL2Renderer(sampleRate, bufferSize))
         return -5;
      emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(0);");
      return 0;
   }

   gInitState = InitState::WebGPURequested;
   reportInitProgress("webgpu_requested", "Creating WebGPU instance and surface");

   gContext = std::make_unique<WebGPUContext>();
   printf("WasmBridge: starting async WebGPU initialization (selector=#canvas)\n");

   bool started = gContext->initializeAsync("#canvas", [sampleRate, bufferSize](bool success)
                                            {
                                               if (!success)
                                               {
                                                  printf("BespokeSynth WASM: Failed to initialize WebGPU\n");
                                                  reportInitProgress("webgpu_failed", "WebGPU initialization failed - browser may not support WebGPU");
                                                  gInitState = InitState::Failed;
                                                  gInitErrorMessage = "WebGPU initialization failed";
                                                  emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-1);");
                                                  return;
                                               }

                                               printf("WasmBridge: WebGPU context ready, proceeding with remaining initialization\n");
                                               gInitState = InitState::WebGPUReady;
                                               reportInitProgress("webgpu_ready", "GPU adapter and device acquired successfully");
                                               gContext->resize(gWidth, gHeight);

<<<<<<< HEAD
                                               // Initialize renderer (backend chosen by gRendererBackendPref)
                                               // Note: Auto (0) currently behaves the same as WebGPU (1) because
                                               // automatic fallback to WebGL2 on WebGPU init failure is not yet
                                               // implemented.  Call bespoke_set_renderer_backend(2) before
                                               // bespoke_init() to explicitly request the WebGL2 path.
                                               printf("WasmBridge: Initializing renderer...\n");
                                               reportInitProgress("renderer_init", "Creating shader pipelines...");
                                               if (gRendererBackendPref == static_cast<int>(bespoke::wasm::RendererBackend::WebGL2)) {
                                                  printf("WasmBridge: Using WebGL2 renderer backend.\n");
                                                  gRenderer = std::make_unique<bespoke::wasm::WebGL2Renderer>();
                                               } else {
                                                  printf("WasmBridge: Using WebGPU renderer backend.\n");
                                                  gRenderer = std::make_unique<WebGPURenderer>(*gContext);
                                               }
                                               if (!gRenderer->initialize())
=======
                                               printf("WasmBridge: Initializing renderer...\n");
                                               reportInitProgress("renderer_init", "Creating shader pipelines...");
                                               auto gpuRenderer = createWebGPURenderer(*gContext);
                                               if (!gpuRenderer->initialize())
>>>>>>> origin/main
                                               {
                                                  reportInitProgress("renderer_failed", "Shader pipeline compilation failed");
                                                  gInitState = InitState::Failed;
                                                  gInitErrorMessage = "Renderer initialization failed";
                                                  emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-2);");
                                                  return;
                                               }

                                               gRenderer = std::move(gpuRenderer);
                                               reportInitProgress("renderer_ready", "All shader pipelines compiled successfully");
                                               gInitState = InitState::RendererReady;

                                               if (!initializeAudioAndControls(sampleRate, bufferSize))
                                               {
                                                  emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-3);");
                                                  return;
                                               }

                                               emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(0);");
                                            });

   if (!started)
   {
      printf("BespokeSynth WASM: Failed to start WebGPU initialization\n");
      reportInitProgress("webgpu_start_failed", "Failed to start WebGPU initialization");
      gInitState = InitState::Failed;
      gInitErrorMessage = "Failed to start async WebGPU initialization";
      return -4;
   }

   // Indicate initialization started asynchronously
   return 1;
}

EMSCRIPTEN_KEEPALIVE void bespoke_process_events(void)
{
   // Process pending WebGPU events to allow async callbacks to fire
   if (gContext)
   {
      gContext->processEvents();
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_shutdown(void)
{
   printf("BespokeSynth WASM: Shutting down\n");

   // Clear controls first
   gKnobs.clear();
   gCanvas.reset();

   // Stop and cleanup audio
   if (gAudioBackend)
   {
      gAudioBackend->stop();
      gAudioBackend->shutdown();
      gAudioBackend.reset();
   }

   // Brief delay to allow audio callback to complete
   // (In WASM, the audio callback should complete quickly after stop)
   for (int i = 0; i < 10 && gAudioCallbackActive.load(); ++i)
   {
      // Just a short busy wait - audio callback should complete within 1-2 buffer periods
   }

   // Cleanup renderer and context
   gRenderer.reset();
   gContext.reset();
   gWebGL2Context.reset();

   // Reset state
   gInitialized = false;
   gInitState = InitState::NotStarted;
   gInitErrorMessage.clear();

   printf("BespokeSynth WASM: Shutdown complete\n");
}

EMSCRIPTEN_KEEPALIVE void bespoke_process_audio(void)
{
   // Audio is handled by SDL callback, nothing to do here
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_sample_rate(int sampleRate)
{
   // Would need to reinitialize audio for this to take effect
   printf("BespokeSynth WASM: Sample rate change requested: %d\n", sampleRate);
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_buffer_size(int bufferSize)
{
   printf("BespokeSynth WASM: Buffer size change requested: %d\n", bufferSize);
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_sample_rate(void)
{
   return gAudioBackend ? gAudioBackend->getSampleRate() : 44100;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_buffer_size(void)
{
   return gAudioBackend ? gAudioBackend->getBufferSize() : 512;
}

// Forward declaration
static void renderDemoPanels();
static void renderFontTestPanel();

EMSCRIPTEN_KEEPALIVE void bespoke_render(void)
{
   // During initialization, process events to allow WebGPU callbacks
   if (gInitState != InitState::FullyInitialized)
   {
      if (gContext)
      {
         gContext->processEvents();
      }
      return;
   }

   if (!gRenderer)
      return;

   gTime += 0.016f;
   gRenderer->beginFrame(gWidth, gHeight, 1.0f, gTime);

   // Clear background
   gRenderer->fillColor(Color(0.12f, 0.12f, 0.14f, 1.0f));
   gRenderer->rect(0, 0, static_cast<float>(gWidth), static_cast<float>(gHeight));
   gRenderer->fill();

   if (gViewMode == 0 && gCanvas)
   {
      // Keep transport play state and audio backend in sync
      if (gCanvas->getTransport() && gAudioBackend)
      {
         const bool playing = gCanvas->getTransport()->isPlaying();
         if (playing && !gAudioBackend->isRunning())
            gAudioBackend->start();
         else if (!playing && gAudioBackend->isRunning())
            gAudioBackend->stop();

         gCanvas->setOutputLevel(gAudioBackend->getOutputLevel());
      }

      // Modular canvas view
      gCanvas->render(*gRenderer, gWidth, gHeight);
   }
   else
   {
      // Legacy demo panel view
      renderDemoPanels();
   }

   if (gFontTestVisible)
      renderFontTestPanel();

   gRenderer->endFrame();
}

// Helper function to render the legacy demo panels view
static void renderDemoPanels()
{
   // Utility: return the 0–1 normalised position of knob[idx]
   auto knobNormalized = [](int idx) -> float {
      if (idx < 0 || idx >= static_cast<int>(gKnobs.size())) return 0.5f;
      const auto& k = *gKnobs[idx];
      float range = k.getMax() - k.getMin();
      return (range > 0.0f) ? (k.getValue() - k.getMin()) / range : 0.5f;
   };
   // Draw title
<<<<<<< HEAD
   gRenderer->fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
   gRenderer->fontSize(24.0f);
   gRenderer->text(20, 40, "BespokeSynth WASM");
=======
   gRenderer->fillColor(UITheme::kTextPrimary);
   gRenderer->fontSize(22.0f);
   gRenderer->text(20, 36, "BespokeSynth WASM");

   // Legacy demo banner
   gRenderer->fillColor(UITheme::kAccentAmber);
   gRenderer->fontSize(11.0f);
   gRenderer->text(20, 56, "Legacy demo panels — Tab switches to Modular Canvas (recommended)");
>>>>>>> origin/main

   // Draw panel tabs
   float tabY = 78.0f;
   float tabHeight = 35.0f;
   float tabWidth = 150.0f;
   float tabSpacing = 5.0f;

   const char* panelNames[] = { "Mixer", "Effects", "Sequencer" };

   for (int i = 0; i < PANEL_COUNT; i++)
   {
      float tabX = 20.0f + i * (tabWidth + tabSpacing);

      // Draw tab background
      if (i == gCurrentPanel)
      {
         gRenderer->fillColor(Color(0.25f, 0.25f, 0.28f, 1.0f));
      }
      else
      {
         gRenderer->fillColor(Color(0.18f, 0.18f, 0.2f, 1.0f));
      }
      gRenderer->roundedRect(tabX, tabY, tabWidth, tabHeight, 5.0f);
      gRenderer->fill();

      // Draw tab border (green if loaded and running, blue if active, default otherwise)
      if (gPanelStatus[i].loaded && gPanelStatus[i].running)
      {
         gRenderer->strokeColor(Color(0.3f, 0.8f, 0.4f, 1.0f)); // Green for running
      }
      else if (i == gCurrentPanel)
      {
         gRenderer->strokeColor(Color(0.4f, 0.7f, 0.9f, 1.0f)); // Blue for active
      }
      else
      {
         gRenderer->strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f)); // Gray for inactive
      }
      gRenderer->strokeWidth(2.0f);
      gRenderer->roundedRect(tabX, tabY, tabWidth, tabHeight, 5.0f);
      gRenderer->stroke();

      // Draw tab label
      if (i == gCurrentPanel)
      {
         gRenderer->fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
      }
      else
      {
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

   // Rebuild the control info cache for this frame so the inspection API is
   // always in sync with what is actually rendered on screen.
   gControlInfoCache.resize(gKnobs.size());
   for (size_t i = 0; i < gKnobs.size(); i++)
   {
      float x = startX + static_cast<float>(i) * spacing;
      gKnobs[i]->render(*gRenderer, x, startY, knobSize);

      ControlInfoEntry& entry = gControlInfoCache[i];
      entry.id    = static_cast<int>(i);
      snprintf(entry.type,  sizeof(entry.type),  "knob");
      snprintf(entry.label, sizeof(entry.label), "%s", gKnobs[i]->getLabel().c_str());
      entry.value = gKnobs[i]->getValue();
      entry.min   = gKnobs[i]->getMin();
      entry.max   = gKnobs[i]->getMax();
      snprintf(entry.unit,  sizeof(entry.unit),  "%s", gKnobs[i]->getUnit().c_str());
      entry.x     = x;
      entry.y     = startY;
      entry.size  = knobSize;
   }

   // Draw cables connecting knobs
   if (gKnobs.size() >= 2)
   {
      gRenderer->drawCableWithSag(
      startX, startY + knobSize * 0.5f + 20,
      startX + spacing, startY + knobSize * 0.5f + 20,
      Color(0.8f, 0.3f, 0.2f, 0.9f),
      3.0f, 0.2f);
   }
   if (gKnobs.size() >= 3)
   {
      gRenderer->drawCableWithSag(
      startX + spacing, startY + knobSize * 0.5f + 30,
      startX + spacing * 2, startY + knobSize * 0.5f + 30,
      Color(0.2f, 0.6f, 0.8f, 0.9f),
      3.0f, 0.25f);
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
   gRenderer->fillColor(UITheme::kTextPrimary);
   gRenderer->fontSize(16.0f);
   gRenderer->text(panelX + 15, panelY + 25, panelNames[gCurrentPanel]);

   // Render panel-specific content
   switch (gCurrentPanel)
   {
      case PANEL_MIXER:
      {
         // Draw mixer controls - sliders and VU meters
         float sliderX = panelX + 30;
         float sliderY = panelY + 50;

         // Derive live slider values from knobs so they update in real time.
         // Channel 1 tracks the Volume knob (idx 1).
         // Channel 2 tracks the Pan knob (idx 3) remapped from -1..1 to 0..1.
         // Master blends Volume and the Frequency position.
         float ch1Val    = knobNormalized(1);
         float ch2Val    = knobNormalized(3);
         float masterVal = (knobNormalized(0) * 0.4f + knobNormalized(1) * 0.6f);

         // Mixer sliders – live values driven by gKnobs
         gRenderer->fillColor(UITheme::kTextPrimary);
         gRenderer->fontSize(12.0f);
         gRenderer->text(sliderX, sliderY - 10, "Channel 1");
         gRenderer->drawSlider(sliderX, sliderY, 200, 20, ch1Val,
                               UITheme::kBgTrack, UITheme::kAccentGreen);

         char vbuf[32]; snprintf(vbuf, sizeof(vbuf), "%.0f%%", ch1Val * 100.0f);
         gRenderer->fillColor(UITheme::kTextValue);
         gRenderer->fontSize(10.0f);
         gRenderer->text(sliderX + 205, sliderY + 5, vbuf);

         gRenderer->fillColor(UITheme::kTextPrimary);
         gRenderer->fontSize(12.0f);
         gRenderer->text(sliderX, sliderY + 40, "Channel 2");
         gRenderer->drawSlider(sliderX, sliderY + 50, 200, 20, ch2Val,
                               UITheme::kBgTrack, UITheme::kAccentCyan);
         snprintf(vbuf, sizeof(vbuf), "%.0f%%", ch2Val * 100.0f);
         gRenderer->fillColor(UITheme::kTextValue);
         gRenderer->fontSize(10.0f);
         gRenderer->text(sliderX + 205, sliderY + 55, vbuf);

         gRenderer->fillColor(UITheme::kTextPrimary);
         gRenderer->fontSize(12.0f);
         gRenderer->text(sliderX, sliderY + 90, "Master");
         gRenderer->drawSlider(sliderX, sliderY + 100, 200, 20, masterVal,
                               UITheme::kBgTrack, UITheme::kAccentMagenta);
         snprintf(vbuf, sizeof(vbuf), "%.0f%%", masterVal * 100.0f);
         gRenderer->fillColor(UITheme::kTextValue);
         gRenderer->fontSize(10.0f);
         gRenderer->text(sliderX + 205, sliderY + 105, vbuf);

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

      case PANEL_EFFECTS:
      {
         // Draw effects controls
         float effectX = panelX + 30;
         float effectY = panelY + 50;

         // Live values: Filter → Reverb Mix / Chorus Depth; Frequency → Delay Time; Pan → Distortion
         float reverbMix     = knobNormalized(2);         // Filter knob normalised
         float delayTime     = knobNormalized(0);         // Frequency knob normalised
         float chorusDepth   = knobNormalized(1);         // Volume knob normalised
         float distortion    = 1.0f - knobNormalized(2); // Inverted filter

         gRenderer->fillColor(UITheme::kTextPrimary);
         gRenderer->fontSize(12.0f);
         gRenderer->text(effectX, effectY - 10, "Reverb Mix");
         gRenderer->drawSlider(effectX, effectY, 250, 20, reverbMix, UITheme::kBgTrack, UITheme::kAccentMagenta);
         { char v[32]; snprintf(v, sizeof(v), "%.0f%%", reverbMix * 100.0f); gRenderer->fillColor(UITheme::kTextValue); gRenderer->fontSize(10.0f); gRenderer->text(effectX + 255, effectY + 5, v); }

         gRenderer->fillColor(UITheme::kTextPrimary);
         gRenderer->fontSize(12.0f);
         gRenderer->text(effectX, effectY + 40, "Delay Time");
         gRenderer->drawSlider(effectX, effectY + 50, 250, 20, delayTime, UITheme::kBgTrack, UITheme::kAccentAmber);
         { char v[32]; snprintf(v, sizeof(v), "%.0f ms", delayTime * 1000.0f); gRenderer->fillColor(UITheme::kTextValue); gRenderer->fontSize(10.0f); gRenderer->text(effectX + 255, effectY + 55, v); }

         gRenderer->fillColor(UITheme::kTextPrimary);
         gRenderer->fontSize(12.0f);
         gRenderer->text(effectX, effectY + 90, "Chorus Depth");
         gRenderer->drawSlider(effectX, effectY + 100, 250, 20, chorusDepth, UITheme::kBgTrack, UITheme::kAccentCyan);
         { char v[32]; snprintf(v, sizeof(v), "%.0f%%", chorusDepth * 100.0f); gRenderer->fillColor(UITheme::kTextValue); gRenderer->fontSize(10.0f); gRenderer->text(effectX + 255, effectY + 105, v); }

         gRenderer->fillColor(UITheme::kTextPrimary);
         gRenderer->fontSize(12.0f);
         gRenderer->text(effectX, effectY + 140, "Distortion");
         gRenderer->drawSlider(effectX, effectY + 150, 250, 20, distortion, UITheme::kBgTrack, UITheme::kAccentMagenta);
         { char v[32]; snprintf(v, sizeof(v), "%.0f%%", distortion * 100.0f); gRenderer->fillColor(UITheme::kTextValue); gRenderer->fontSize(10.0f); gRenderer->text(effectX + 255, effectY + 155, v); }

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
         for (int i = 0; i < 10; i++)
         {
            float x1 = vizX + i * vizW / 10;
            float x2 = vizX + (i + 1) * vizW / 10;
            float y1 = vizY + vizH / 2 + sinf(i * 0.5f) * 30;
            float y2 = vizY + vizH / 2 + sinf((i + 1) * 0.5f) * 30;
            gRenderer->line(x1, y1, x2, y2);
         }
         break;
      }

      case PANEL_SEQUENCER:
      {
         // Draw sequencer controls
         float seqX = panelX + 30;
         float seqY = panelY + 50;

         // BPM tracks the Frequency knob (100–900 Hz → mapped to 60–200 BPM).
         // Swing tracks the Pan knob (-1..1 → 0..1).
         float bpmNorm  = knobNormalized(0);
         float swingNorm = knobNormalized(3);
         float bpmDisplay = 60.0f + bpmNorm * 140.0f; // 60–200 BPM

         gRenderer->fillColor(UITheme::kTextPrimary);
         gRenderer->fontSize(12.0f);
         gRenderer->text(seqX, seqY - 10, "BPM");
         gRenderer->drawSlider(seqX, seqY, 150, 20, bpmNorm,
                               Color(0.25f, 0.25f, 0.28f, 1.0f),
                               Color(0.5f, 0.8f, 0.4f, 1.0f));
         { char v[32]; snprintf(v, sizeof(v), "%.0f BPM", bpmDisplay); gRenderer->fillColor(UITheme::kTextValue); gRenderer->fontSize(10.0f); gRenderer->text(seqX + 155, seqY + 5, v); }

         gRenderer->fillColor(UITheme::kTextPrimary);
         gRenderer->fontSize(12.0f);
         gRenderer->text(seqX + 200, seqY - 10, "Swing");
         gRenderer->drawSlider(seqX + 200, seqY, 150, 20, swingNorm,
                               Color(0.25f, 0.25f, 0.28f, 1.0f),
                               Color(0.8f, 0.7f, 0.4f, 1.0f));
         { char v[32]; snprintf(v, sizeof(v), "%.0f%%", swingNorm * 100.0f); gRenderer->fillColor(UITheme::kTextValue); gRenderer->fontSize(10.0f); gRenderer->text(seqX + 355, seqY + 5, v); }

         // Draw step sequencer grid
         float gridX = seqX;
         float gridY = seqY + 50;

         // Sequencer grid constants
         const float STEP_WIDTH = 35.0f;
         const float STEP_HEIGHT = 30.0f;
         const int NUM_STEPS = 16;
         const int NUM_ROWS = 4;
         const int STEP_PATTERN_INTERVAL = 3; // Pattern interval for demo

         gRenderer->text(gridX, gridY - 10, "Step Sequencer (16 steps x 4 notes)");

         for (int row = 0; row < NUM_ROWS; row++)
         {
            for (int step = 0; step < NUM_STEPS; step++)
            {
               float sx = gridX + step * STEP_WIDTH;
               float sy = gridY + row * STEP_HEIGHT;

               // Alternate pattern for demo
               bool active = (step + row) % STEP_PATTERN_INTERVAL == 0;

               if (active)
               {
                  gRenderer->fillColor(Color(0.4f, 0.7f, 0.5f, 1.0f));
               }
               else
               {
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
   const bool transportPlaying = gCanvas && gCanvas->getTransport() && gCanvas->getTransport()->isPlaying();
   snprintf(statusText, sizeof(statusText),
            "Sample rate: %d Hz | Buffer: %d | %s | Panel: %s | Tab: Modular Canvas",
            bespoke_get_sample_rate(),
            bespoke_get_buffer_size(),
            transportPlaying ? "Playing" : "Stopped",
            panelNames[gCurrentPanel]);

   gRenderer->text(20, static_cast<float>(gHeight) - 20, statusText);

   // View switch hint
   gRenderer->fillColor(Color(0.5f, 0.5f, 0.55f, 0.7f));
   gRenderer->fontSize(10.0f);
   gRenderer->text(static_cast<float>(gWidth) - 150, 20, "Tab: Modular Canvas");
}

static void renderFontTestPanel()
{
   if (!gRenderer)
      return;

   const float panelX = 16.0f;
   const float panelY = 52.0f;
   const float panelW = static_cast<float>(gWidth) - 32.0f;
   const float panelH = static_cast<float>(gHeight) - 84.0f;

   gRenderer->fillColor(Color(0.06f, 0.06f, 0.08f, 0.94f));
   gRenderer->roundedRect(panelX, panelY, panelW, panelH, 8.0f);
   gRenderer->fill();

   gRenderer->strokeColor(Color(0.35f, 0.35f, 0.42f, 1.0f));
   gRenderer->strokeWidth(1.0f);
   gRenderer->roundedRect(panelX, panelY, panelW, panelH, 8.0f);
   gRenderer->stroke();

   const char* kSharp = "C\xe2\x99\xaf";
   const char* kFlat = "B\xe2\x99\xad";
   char musicalLine[64];
   snprintf(musicalLine, sizeof(musicalLine), "Musical: %smaj  %smin  A#  Db", kSharp, kFlat);

   const char* rows[] = {
      "Font Test Panel",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
      "abcdefghijklmnopqrstuvwxyz",
      "0123456789  !@#$%^&*()-_=+<>?/\\|~",
      "In Out Pitch Gate Cutoff Resonance",
      "440 Hz  -12 dB  50%  1.50x",
      musicalLine,
   };

   float y = panelY + 18.0f;
   for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
   {
      gRenderer->fillColor(i == 0 ? UITheme::kTextValue : UITheme::kTextSecondary);
      gRenderer->fontSize(i == 0 ? 13.0f : 10.0f);
      gRenderer->text(panelX + 12.0f, y, rows[i]);
      y += gRenderer->textHeight() + 5.0f;
   }

   gRenderer->fillColor(UITheme::kTextSecondary);
   gRenderer->fontSize(10.0f);
   float gx = panelX + 12.0f;
   float gy = y + 8.0f;
   for (int c = 32; c <= 126; ++c)
   {
      char ch[2] = { static_cast<char>(c), '\0' };
      const float advance = gRenderer->textWidth(ch) + 2.0f;
      gRenderer->text(gx, gy, ch);
      gx += advance;
      if (gx > panelX + panelW - 18.0f)
      {
         gx = panelX + 12.0f;
         gy += gRenderer->textHeight() + 4.0f;
      }
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_resize(int width, int height)
{
   gWidth = width;
   gHeight = height;

   if (gContext)
      gContext->resize(width, height);
   if (gWebGL2Context)
      gWebGL2Context->resize(width, height);

   printf("BespokeSynth WASM: Resized to %dx%d\n", width, height);
}

static int gMouseX = 0;
static int gMouseY = 0;
static bool gMouseDown = false;

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_move(int x, int y)
{
   int prevX = gMouseX;
   int prevY = gMouseY;
   gMouseX = x;
   gMouseY = y;

   // Route to modular canvas
   if (gViewMode == 0 && gCanvas)
   {
      gCanvas->onMouseMove(static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(prevX), static_cast<float>(prevY));
      return;
   }

   // Handle knob dragging
   if (gMouseDown)
   {
      for (auto& knob : gKnobs)
      {
         knob->onMouseDrag(static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(prevX), static_cast<float>(prevY));
      }
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_down(int x, int y, int button)
{
   gMouseDown = true;
   gMouseX = x;
   gMouseY = y;

   // Route to modular canvas
   if (gViewMode == 0 && gCanvas)
   {
      gCanvas->onMouseDown(static_cast<float>(x), static_cast<float>(y), button);
      return;
   }

   // Check panel tab clicks
   float tabY = 70.0f;
   float tabHeight = 35.0f;
   float tabWidth = 150.0f;
   float tabSpacing = 5.0f;

   if (y >= tabY && y <= tabY + tabHeight)
   {
      for (int i = 0; i < PANEL_COUNT; i++)
      {
         float tabX = 20.0f + i * (tabWidth + tabSpacing);
         if (x >= tabX && x <= tabX + tabWidth)
         {
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
   float startY = 130.0f; // Updated to match render
   float spacing = 120.0f;

   for (size_t i = 0; i < gKnobs.size(); i++)
   {
      float kx = startX + i * spacing;
      if (gKnobs[i]->hitTest(static_cast<float>(x), static_cast<float>(y), kx, startY, knobSize))
      {
         gKnobs[i]->onMouseDown(static_cast<float>(x), static_cast<float>(y), kx, startY, knobSize);
         break;
      }
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_up(int x, int y, int button)
{
   gMouseDown = false;

   // Route to modular canvas
   if (gViewMode == 0 && gCanvas)
   {
      gCanvas->onMouseUp(static_cast<float>(x), static_cast<float>(y), button);
      return;
   }

   for (auto& knob : gKnobs)
   {
      knob->onMouseUp();
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_wheel(float deltaX, float deltaY)
{
   // Route to modular canvas for zoom
   if (gViewMode == 0 && gCanvas)
   {
      gCanvas->onMouseWheel(deltaX, deltaY, static_cast<float>(gMouseX), static_cast<float>(gMouseY));
      return;
   }

   // Handle scroll on knobs
   float knobSize = 80.0f;
   float startX = 100.0f;
   float startY = 130.0f; // Updated to match render
   float spacing = 120.0f;

   for (size_t i = 0; i < gKnobs.size(); i++)
   {
      float kx = startX + i * spacing;
      if (gKnobs[i]->hitTest(static_cast<float>(gMouseX), static_cast<float>(gMouseY), kx, startY, knobSize))
      {
         gKnobs[i]->onScroll(deltaY);
         break;
      }
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_key_down(int keyCode, int modifiers)
{
   // Tab key switches between modular canvas and demo panels
   if (keyCode == KEY_TAB)
   {
      gViewMode = (gViewMode == 0) ? 1 : 0;
      printf("BespokeSynth WASM: Switched to %s view\n",
             gViewMode == 0 ? "Modular Canvas" : "Demo Panels");
      return;
   }

   // Route to modular canvas
   if (gViewMode == 0 && gCanvas)
   {
      gCanvas->onKeyDown(keyCode, modifiers);
   }

   // Handle shift for fine mode
   if (keyCode == KEY_SHIFT)
   {
      for (auto& knob : gKnobs)
      {
         knob->setFineMode(true);
      }
   }

   // Space toggles transport; audio backend follows play state
   if (keyCode == KEY_SPACE && gAudioBackend)
   {
      if (gViewMode != 0 && gCanvas && gCanvas->getTransport())
         gCanvas->getTransport()->setPlaying(!gCanvas->getTransport()->isPlaying());

      if (gCanvas && gCanvas->getTransport())
      {
         if (gCanvas->getTransport()->isPlaying())
            gAudioBackend->start();
         else
            gAudioBackend->stop();
      }
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_key_up(int keyCode, int modifiers)
{
   if (keyCode == KEY_SHIFT)
   {
      for (auto& knob : gKnobs)
      {
         knob->setFineMode(false);
      }
   }
}

EMSCRIPTEN_KEEPALIVE int bespoke_create_module(const char* type, float x, float y)
{
   if (gCanvas)
   {
      int id = gCanvas->createModule(type, x, y);
      printf("BespokeSynth WASM: Created module '%s' (id=%d) at (%.1f, %.1f)\n", type, id, x, y);
      return id;
   }
   printf("BespokeSynth WASM: Cannot create module - canvas not initialized\n");
   return -1;
}

EMSCRIPTEN_KEEPALIVE void bespoke_delete_module(int moduleId)
{
   if (gCanvas)
   {
      gCanvas->deleteModule(moduleId);
   }
   printf("BespokeSynth WASM: Delete module %d\n", moduleId);
}

EMSCRIPTEN_KEEPALIVE void bespoke_connect_modules(int sourceId, int destId)
{
   if (gCanvas)
   {
      gCanvas->connectModules(sourceId, 0, destId, 0);
   }
   printf("BespokeSynth WASM: Connect %d -> %d\n", sourceId, destId);
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_view_mode(void)
{
   return gViewMode;
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_view_mode(int mode)
{
   gViewMode = (mode == 0) ? 0 : 1;
   printf("BespokeSynth WASM: Set view mode to %d (%s)\n",
          gViewMode, gViewMode == 0 ? "Modular Canvas" : "Demo Panels");
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_control_value(int moduleId, const char* controlName, float value)
{
   // Try canvas modules first
   if (gCanvas)
   {
      auto* mod = gCanvas->getModule(moduleId);
      if (mod)
      {
         mod->setControlValue(controlName, value);
         return;
      }
   }
   // Fallback: control knobs directly
   if (moduleId >= 0 && moduleId < static_cast<int>(gKnobs.size()))
   {
      gKnobs[moduleId]->setValue(value);
   }
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_control_value(int moduleId, const char* controlName)
{
   // Try canvas modules first
   if (gCanvas)
   {
      auto* mod = gCanvas->getModule(moduleId);
      if (mod)
      {
         return mod->getControlValue(controlName);
      }
   }
   // Fallback: read from knobs
   if (moduleId >= 0 && moduleId < static_cast<int>(gKnobs.size()))
   {
      return gKnobs[moduleId]->getValue();
   }
   return 0.0f;
}

EMSCRIPTEN_KEEPALIVE int bespoke_save_state(const char* filename)
{
   printf("BespokeSynth WASM: Save state to '%s'\n", filename);
   return 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_load_state(const char* filename)
{
   printf("BespokeSynth WASM: Load state from '%s'\n", filename);
   return 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_state_json(void)
{
   // TODO: Return actual state
   return "{}";
}

EMSCRIPTEN_KEEPALIVE int bespoke_load_state_json(const char* json)
{
   printf("BespokeSynth WASM: Load state from JSON\n");
   return 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_play(void)
{
   if (gAudioBackend)
   {
      gAudioBackend->start();
   }
   if (gCanvas && gCanvas->getTransport())
   {
      gCanvas->getTransport()->setPlaying(true);
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_stop(void)
{
   if (gAudioBackend)
   {
      gAudioBackend->stop();
   }
   if (gCanvas && gCanvas->getTransport())
   {
      gCanvas->getTransport()->setPlaying(false);
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_tempo(float bpm)
{
   if (gCanvas && gCanvas->getTransport())
   {
      gCanvas->getTransport()->setBPM(bpm);
   }
   printf("BespokeSynth WASM: Set tempo to %.1f BPM\n", bpm);
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_tempo(void)
{
   if (gCanvas && gCanvas->getTransport())
   {
      return gCanvas->getTransport()->getBPM();
   }
   return 120.0f;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_version(void)
{
   return kVersion;
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_cpu_load(void)
{
   // TODO: Calculate actual CPU load
   return 0.0f;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_module_count(void)
{
   if (gCanvas)
   {
      return gCanvas->getModuleCount();
   }
   return static_cast<int>(gKnobs.size());
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_panel(int panelIndex)
{
   if (panelIndex >= 0 && panelIndex < PANEL_COUNT)
   {
      int prevPanel = gCurrentPanel;
      gCurrentPanel = panelIndex;
      printf("DEBUG [API] Panel switch via bespoke_set_panel: From:%s To:%s\n",
             getPanelName(prevPanel), getPanelName(panelIndex));
      logPanelStatus(panelIndex, "ACTIVATED");
   }
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_panel(void)
{
   return gCurrentPanel;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_panel_count(void)
{
   return PANEL_COUNT;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_panel_name(int panelIndex)
{
   return getPanelName(panelIndex);
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_panel_loaded(int panelIndex)
{
   if (panelIndex >= 0 && panelIndex < PANEL_COUNT)
   {
      return gPanelStatus[panelIndex].loaded ? 1 : 0;
   }
   return 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_panel_running(int panelIndex)
{
   if (panelIndex >= 0 && panelIndex < PANEL_COUNT)
   {
      return gPanelStatus[panelIndex].running ? 1 : 0;
   }
   return 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_panel_frame_count(int panelIndex)
{
   if (panelIndex >= 0 && panelIndex < PANEL_COUNT)
   {
      return gPanelStatus[panelIndex].frameCount;
   }
   return 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_log_all_panels_status(void)
{
   printf("\n=== DEBUG: All Panels Status ===\n");
   for (int i = 0; i < PANEL_COUNT; i++)
   {
      logPanelStatus(i, "STATUS CHECK");
   }
   printf("=== End Panel Status ===\n\n");
}

// New initialization state query functions
EMSCRIPTEN_KEEPALIVE int bespoke_get_init_state(void)
{
   return static_cast<int>(gInitState);
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_init_error(void)
{
   return gInitErrorMessage.c_str();
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_fully_initialized(void)
{
   return (gInitState == InitState::FullyInitialized) ? 1 : 0;
}

// ---- Control enumeration / inspection API ----

EMSCRIPTEN_KEEPALIVE int bespoke_get_control_count(void)
{
   return static_cast<int>(gControlInfoCache.size());
}

// Append a JSON-escaped version of `src` into `dst`, stopping before `end`.
static char* jsonEscape(char* dst, const char* end, const char* src)
{
   for (; *src && dst < end - 1; ++src)
   {
      if (*src == '"' || *src == '\\')
      {
         if (dst < end - 2) { *dst++ = '\\'; }
         else break;
      }
      *dst++ = *src;
   }
   return dst;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_control_info(int index)
{
   // Static buffer – WASM runs single-threaded; caller must consume before
   // the next call (which is guaranteed by the JS overlay loop).
   static char sJsonBuf[512];

   if (index < 0 || index >= static_cast<int>(gControlInfoCache.size()))
   {
      snprintf(sJsonBuf, sizeof(sJsonBuf), "{}");
      return sJsonBuf;
   }

   const ControlInfoEntry& e = gControlInfoCache[index];

   // Build JSON with proper escaping for the string fields.
   char* p   = sJsonBuf;
   char* end = sJsonBuf + sizeof(sJsonBuf) - 1;

   p += snprintf(p, end - p, "{\"id\":%d,\"type\":\"%s\",\"label\":\"", e.id, e.type);
   p  = jsonEscape(p, end, e.label);
   p += snprintf(p, end - p, "\",\"value\":%.6f,\"min\":%.6f,\"max\":%.6f,\"unit\":\"",
                 e.value, e.min, e.max);
   p  = jsonEscape(p, end, e.unit);
   p += snprintf(p, end - p, "\",\"x\":%.2f,\"y\":%.2f,\"size\":%.2f}",
                 e.x, e.y, e.size);
   *p = '\0';

   return sJsonBuf;
}

// ---- Runtime theming API ----

EMSCRIPTEN_KEEPALIVE void bespoke_set_theme_color(int colorId, float r, float g, float b, float a)
{
   auto id = static_cast<bespoke::wasm::ThemeColorId>(colorId);
   gRuntimeTheme.setColor(id, bespoke::wasm::Color(r, g, b, a));
}

EMSCRIPTEN_KEEPALIVE void bespoke_reset_theme(void)
{
   gRuntimeTheme.reset();
}

<<<<<<< HEAD
// ---- Renderer backend selection API ----

/**
 * Set the preferred renderer backend before calling bespoke_init().
 * Values: 0 = Auto (WebGPU preferred, WebGL2 fallback),
 *         1 = WebGPU,
 *         2 = WebGL2
 */
EMSCRIPTEN_KEEPALIVE void bespoke_set_renderer_backend(int backend)
{
   gRendererBackendPref = backend;
   printf("WasmBridge: renderer backend preference set to %d\n", backend);
}

/**
 * Returns the currently active renderer backend enum value
 * (1=WebGPU, 2=WebGL2, or 0 if not yet initialized).
 */
EMSCRIPTEN_KEEPALIVE int bespoke_get_renderer_backend(void)
{
   if (!gRenderer) return 0;
   return static_cast<int>(gRenderer->getBackend());
}

// ---- Screenshot / canvas capture API ----

/**
 * Capture the current frame as an RGBA8 pixel buffer.
 * Returns a pointer to a heap-allocated buffer (caller must free it with
 * bespoke_free_capture_buffer()), or 0 on failure.
 * outWidth and outHeight are written via the provided int* pointers.
 *
 * NOTE: For the WebGPU backend this currently returns 0 (WebGPU requires
 * an asynchronous buffer readback that is not yet implemented).
 * The WebGL2 backend returns full RGBA data immediately.
 */
EMSCRIPTEN_KEEPALIVE uint8_t* bespoke_capture_frame(int* outWidth, int* outHeight)
{
   if (!outWidth || !outHeight) return nullptr;
   // Pre-zero so that a nullptr return always produces valid (0,0) dimensions.
   *outWidth  = 0;
   *outHeight = 0;
   if (!gRenderer) return nullptr;
   int w = 0, h = 0;
   uint8_t* pixels = gRenderer->captureFrame(w, h);
   *outWidth  = w;
   *outHeight = h;
   return pixels;
}

/**
 * Free a pixel buffer previously returned by bespoke_capture_frame().
 */
EMSCRIPTEN_KEEPALIVE void bespoke_free_capture_buffer(uint8_t* buf)
{
   delete[] buf;
=======
EMSCRIPTEN_KEEPALIVE void bespoke_set_renderer_backend(int backend)
{
   if (gInitState == InitState::NotStarted)
      gRendererBackend = (backend == 1) ? RendererBackendType::WebGL2 : RendererBackendType::WebGPU;
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_webgl2_supported(void)
{
   return WebGL2Context::probeSupport("#canvas") ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_webgl2_error(void)
{
   return WebGL2Context::getLastError();
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_renderer_backend(void)
{
   return static_cast<int>(gRendererBackend);
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_webgl_debug_mode(int mode)
{
   gWebGLDebugMode = static_cast<WebGLDebugMode>(mode);
   if (gRenderer)
      gRenderer->setDebugMode(gWebGLDebugMode);
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_webgl_debug_mode(void)
{
   return static_cast<int>(gWebGLDebugMode);
}

EMSCRIPTEN_KEEPALIVE int bespoke_capture_screenshot(int* outWidth, int* outHeight)
{
   if (!gInitialized || !gRenderer)
      return -1;

   gScreenshotPixels.clear();
   gScreenshotWidth = 0;
   gScreenshotHeight = 0;

   gBespokePendingScreenshotCapture = true;
   bespoke_render();
   gBespokePendingScreenshotCapture = false;

   if (outWidth)
      *outWidth = gWidth;
   if (outHeight)
      *outHeight = gHeight;

   if (gRendererBackend == RendererBackendType::WebGL2)
      return 1;

   if (gContext)
   {
      std::vector<uint8_t> pixels;
      int w = 0;
      int h = 0;
      if (gContext->readCapturedPixels(pixels, w, h))
      {
         gScreenshotPixels = std::move(pixels);
         gScreenshotWidth = w;
         gScreenshotHeight = h;
         if (outWidth)
            *outWidth = w;
         if (outHeight)
            *outHeight = h;
         return 0;
      }
   }

   return -1;
}

EMSCRIPTEN_KEEPALIVE const unsigned char* bespoke_get_screenshot_pixels(int* outByteLength)
{
   if (outByteLength)
      *outByteLength = static_cast<int>(gScreenshotPixels.size());
   return gScreenshotPixels.empty() ? nullptr : gScreenshotPixels.data();
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_render_test_mode(int enabled)
{
   gRenderTestMode = enabled != 0;
   if (!gCanvas)
      return;

   gViewMode = 0;
   gCanvas->setupCanonicalRenderTestScene();
   gOscillatorModuleId = gCanvas->findFirstModuleOfType("oscillator");
   gGainModuleId = gCanvas->findFirstModuleOfType("gain");
   if (gRenderTestMode)
      gFontTestVisible = false;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_render_test_mode(void)
{
   return gRenderTestMode ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_font_test_visible(int visible)
{
   gFontTestVisible = visible != 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_font_test_visible(void)
{
   return gFontTestVisible ? 1 : 0;
>>>>>>> origin/main
}

} // extern "C"
