/**
 * BespokeSynth WASM - Init / lifecycle C ABI
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ScriptNoteQueue.h"
#include "BespokeWasm/WasmBridge.h"
#include "BespokeWasm/WasmBridgeInternal.h"
#include "BespokeWasm/Renderer2D.h"
#include "BespokeWasm/SdfFont.h"
#include "BespokeWasm/WebGPUContext.h"
#include "BespokeWasm/WebGL2Context.h"
#include "BespokeWasm/SDL2AudioBackend.h"
#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/AudioRemote.h"
#include "BespokeWasm/ResourceLifetime.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/WasmAudioEngine.h"
#include "BespokeWasm/WasmRuntimeState.h"
#include "BespokeWasm/Theme.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <emscripten.h>

using namespace bespoke::wasm;

// clang-format off
EM_JS(void, bespoke_js_on_init_progress, (const char* step, const char* detail), {
   if (typeof window !== 'undefined' && window['__bespoke_on_init_progress'])
      window['__bespoke_on_init_progress'](UTF8ToString(step), UTF8ToString(detail));
});

EM_JS(void, bespoke_js_on_init_complete, (int status), {
   if (typeof window !== 'undefined' && window['__bespoke_on_init_complete'])
      window['__bespoke_on_init_complete'](status);
});

// emdawnwebgpu calls navigator.gpu.requestAdapter() unguarded; without WebGPU that throws
// out of bespoke_init instead of failing into the WebGL2 fallback.
EM_JS(int, bespoke_js_has_webgpu, (), {
   return (typeof navigator !== 'undefined' && navigator['gpu']) ? 1 : 0;
});

EM_JS(void, bespoke_js_on_canvas_notice, (const char* message), {
   if (typeof window !== 'undefined' && window['__bespoke_on_canvas_notice'])
      window['__bespoke_on_canvas_notice'](UTF8ToString(message));
});

// A snapshot names samples that are not resident (evicted, then undo). The host reads the
// hashes (bespoke_take_missing_samples) and loads them from OPFS; deferred, since this runs
// inside a publish.
EM_JS(void, bespoke_js_on_samples_missing, (), {
   if (typeof window !== 'undefined' && window['__bespoke_on_samples_missing'])
      queueMicrotask(function() { window['__bespoke_on_samples_missing'](); });
});
// clang-format on

namespace bespoke
{
   namespace wasm
   {
      void reportInitProgress(const char* step, const char* detail)
      {
         printf("[InitProgress] %s: %s\n", step, detail);
         bespoke_js_on_init_progress(step, detail);
      }

      void reportInitComplete(int status)
      {
         bespoke_js_on_init_complete(status);
      }

      void wasmAudioCallback(const float* const* input, float* const* output,
                             int numInputChannels, int numOutputChannels, int numSamples)
      {
         processWasmAudio(input, output, numInputChannels, numOutputChannels, numSamples);
      }

      bool initializeAudioAndControls(int sampleRate, int bufferSize)
      {
         auto& state = runtimeState();
         printf("WasmBridge: Initializing audio backend...\n");
         reportInitProgress("audio_init", "Opening audio device...");
         state.audioBackend = std::make_unique<SDL2AudioBackend>();
         if (!state.audioBackend->initialize(sampleRate, bufferSize, 2, 0))
         {
            printf("BespokeSynth WASM: Failed to initialize audio\n");
            reportInitProgress("audio_failed", "Audio device initialization failed");
            state.initState = InitState::Failed;
            state.initErrorMessage = "Audio backend initialization failed";
            return false;
         }

         printf("WasmBridge: Audio backend initialized successfully\n");
         syncSampleStoreRate();
         reportInitProgress("audio_ready", "Audio device opened successfully");
         state.initState = InitState::AudioReady;
         state.audioBackend->setCallback(wasmAudioCallback);
         reportInitProgress("audio_ready", "Audio ready - click play to start");

         if (state.legacyPanelsEnabled)
            ensureLegacyDemoKnobs();

         printf("WasmBridge: Creating modular canvas...\n");
         reportInitProgress("controls_init", "Creating modular canvas...");
         state.canvas = std::make_unique<ModuleCanvas>();
         state.canvas->setNoticeListener([](const std::string& message)
                                         {
                                            bespoke_js_on_canvas_notice(message.c_str());
                                         });
         state.audioGraphEngine = std::make_unique<AudioGraphEngine>();
         state.audioGraphEngine->prepareForBlock(std::max(bufferSize, 2048), 128, 32);
         prepareWasmAudioScratch(std::max(bufferSize, 2048));
         // Size engine runtime state on every publish (spawn/delete/undo), so
         // processBlock never grows an arena inside the audio callback. While the
         // AudioWorklet engine plays, it sizes its own state; ours stays idle.
         state.canvas->setSnapshotListener([](const AudioGraphSnapshot& graph)
                                           {
                                              if (audioRemoteActive())
                                                 notifyAudioGraphPublished();
                                              else if (auto* engine = runtimeState().audioGraphEngine.get())
                                                 engine->prepareRuntimeStates(graph);
                                              // No pthreads: the next block on this instance adopts `graph`
                                              // before it reads a param, so nothing older can be processed.
                                              // The worklet drops its copies when the host relays evictions.
                                              collectUnusedResources(graph.generation);
                                              if (SampleStore::instance().hasMissing())
                                                 bespoke_js_on_samples_missing();
                                           });
         // Knob drags, MIDI CC and transport edits skip the publish: one queued change each.
         state.canvas->setParamSink([](const ParamChange& change)
                                    {
                                       return queueParamChange(change);
                                    });

         const int oscId = state.canvas->createModule("oscillator", 100, 150);
         const int filterId = state.canvas->createModule("filter", 300, 155);
         const int gainId = state.canvas->createModule("gain", 500, 160);
         const int lfoId = state.canvas->createModule("lfo", 100, 310);
         const int outputId = state.canvas->createModule("output", 680, 170);
         state.oscillatorModuleId = oscId;
         state.gainModuleId = gainId;

         if (oscId > 0 && filterId > 0 && gainId > 0 && lfoId > 0 && outputId > 0)
         {
            state.canvas->connectModules(oscId, 0, filterId, 0);
            state.canvas->connectModules(filterId, 0, gainId, 0);
            state.canvas->connectModules(gainId, 0, outputId, 0);
            state.canvas->connectModules(lfoId, 0, filterId, 1);

            // Match previous demo-knob seeds (Knob stored 0.5/0.7/0.3 after setRange).
            if (auto* osc = state.canvas->getModule(oscId))
            {
               osc->setControlValue("frequency", 0.5f);
               osc->setControlValue("volume", 0.7f);
            }
            if (auto* filter = state.canvas->getModule(filterId))
            {
               filter->setControlValue("cutoff", 0.3f);
               filter->setControlValue("resonance", 0.55f);
            }
            if (auto* gain = state.canvas->getModule(gainId))
               gain->setControlValue("gain", 0.7f);
            if (auto* lfo = state.canvas->getModule(lfoId))
            {
               lfo->setControlValue("rate", 2.0f);
               lfo->setControlValue("depth", 0.35f);
            }
         }

         // The default patch is the starting point, not an undoable edit.
         state.canvas->clearUndoHistory();

         state.initState = InitState::FullyInitialized;
         state.initialized = true;
         reportInitProgress("init_complete", "All subsystems ready");
         printf("BespokeSynth WASM: Initialization complete - all subsystems ready\n");
         return true;
      }
   } // namespace wasm
} // namespace bespoke

bespoke::wasm::Color bespoke::wasm::RuntimeTheme::resolve(bespoke::wasm::ThemeColorId id) const
{
   using namespace bespoke::wasm::UITheme;
   int i = static_cast<int>(id);
   if (i >= 0 && i < static_cast<int>(bespoke::wasm::ThemeColorId::Count) && active[i])
      return colors[i];
   switch (id)
   {
      case bespoke::wasm::ThemeColorId::BgDark: return kBgDark;
      case bespoke::wasm::ThemeColorId::BgPanel: return kBgPanel;
      case bespoke::wasm::ThemeColorId::BgTrack: return kBgTrack;
      case bespoke::wasm::ThemeColorId::BgElevated: return kBgElevated;
      case bespoke::wasm::ThemeColorId::TextPrimary: return kTextPrimary;
      case bespoke::wasm::ThemeColorId::TextSecondary: return kTextSecondary;
      case bespoke::wasm::ThemeColorId::TextValue: return kTextValue;
      case bespoke::wasm::ThemeColorId::AccentCyan: return kAccentCyan;
      case bespoke::wasm::ThemeColorId::AccentMagenta: return kAccentMagenta;
      case bespoke::wasm::ThemeColorId::AccentAmber: return kAccentAmber;
      case bespoke::wasm::ThemeColorId::AccentGreen: return kAccentGreen;
      case bespoke::wasm::ThemeColorId::KnobBg: return kKnobBg;
      case bespoke::wasm::ThemeColorId::KnobFg: return kKnobFg;
      case bespoke::wasm::ThemeColorId::KnobIndicator: return kKnobIndicator;
      case bespoke::wasm::ThemeColorId::KnobRim: return kKnobRim;
      default: return kTextPrimary;
   }
}

/** Context + renderer on #canvas; leaves initState alone so a recovery can reuse it. */
static bool createWebGL2RendererOnCanvas(std::string& error)
{
   auto& state = runtimeState();
   state.webGL2Context = std::make_unique<WebGL2Context>();
   WebGL2Context::setPreserveDrawingBuffer(state.webGLPreserveDrawingBuffer);
   if (!state.webGL2Context->initialize("#canvas"))
   {
      error = WebGL2Context::getLastError();
      if (error.empty())
         error = "WebGL2 context creation failed";
      state.webGL2Context.reset();
      return false;
   }
   state.webGL2Context->resize(state.width, state.height);

   auto glRenderer = createWebGL2Renderer(*state.webGL2Context);
   glRenderer->setDebugMode(state.webGLDebugMode);
   if (!glRenderer->initialize())
   {
      error = "WebGL2 renderer initialization failed — shader compilation error";
      state.webGL2Context.reset();
      return false;
   }
   state.renderer = std::move(glRenderer);
   return true;
}

static bool initializeWebGL2Renderer(int sampleRate, int bufferSize)
{
   auto& state = runtimeState();
   state.initState = InitState::WebGL2Requested;
   reportInitProgress("webgl_requested", "Creating WebGL2 context on #canvas");

   std::string error;
   if (!createWebGL2RendererOnCanvas(error))
   {
      state.initState = InitState::Failed;
      state.initErrorMessage = error;
      reportInitProgress(state.webGL2Context ? "renderer_failed" : "webgl_failed", error.c_str());
      return false;
   }
   state.initState = InitState::WebGL2Ready;

   const auto& caps = state.webGL2Context->getCapabilities();
   char readyDetail[256];
   snprintf(readyDetail, sizeof(readyDetail),
            "WebGL2 context ready (maxTexture=%d, VAO=%s, instancing=%s)",
            caps.maxTextureSize,
            caps.vertexArrayObject ? "yes" : "no",
            caps.instancedArrays ? "yes" : "no");
   reportInitProgress("webgl_ready", readyDetail);

   state.initState = InitState::RendererReady;
   reportInitProgress("renderer_ready", "WebGL2 renderer ready");
   return initializeAudioAndControls(sampleRate, bufferSize);
}

extern "C" {

EMSCRIPTEN_KEEPALIVE int bespoke_init(int width, int height, int sampleRate, int bufferSize)
{
   auto& state = runtimeState();
   printf("BespokeSynth WASM: Initializing (%dx%d, %dHz, %d samples, backend=%d)\n",
          width, height, sampleRate, bufferSize, static_cast<int>(state.rendererBackend));
   reportInitProgress("init_start", "Beginning initialization");

   if (state.initState != InitState::NotStarted)
   {
      printf("BespokeSynth WASM: Already initialized or in progress (state=%d)\n", static_cast<int>(state.initState));
      return state.initState == InitState::FullyInitialized ? 0 : 1;
   }

   state.width = width;
   state.height = height;

   if (state.rendererBackend == RendererBackendType::WebGL2)
   {
      if (!initializeWebGL2Renderer(sampleRate, bufferSize))
         return -5;
      reportInitComplete(0);
      return 0;
   }

   if (!bespoke_js_has_webgpu())
   {
      printf("BespokeSynth WASM: navigator.gpu is unavailable\n");
      reportInitProgress("webgpu_start_failed", "WebGPU is not available in this browser");
      state.initState = InitState::Failed;
      state.initErrorMessage = "WebGPU is not available in this browser (navigator.gpu is missing)";
      return -4;
   }

   state.initState = InitState::WebGPURequested;
   reportInitProgress("webgpu_requested", "Creating WebGPU instance and surface");

   state.context = std::make_unique<WebGPUContext>();
   printf("WasmBridge: starting async WebGPU initialization (selector=#canvas)\n");

   WebGPUContext* context = state.context.get();
   bool started = state.context->initializeAsync("#canvas", [sampleRate, bufferSize, context](bool success)
                                                 {
                                                    auto& rt = runtimeState();
                                                    if (rt.context.get() != context)
                                                       return; // shut down (or retried) meanwhile
                                                    if (!success)
                                                    {
                                                       printf("BespokeSynth WASM: Failed to initialize WebGPU\n");
                                                       reportInitProgress("webgpu_failed", "WebGPU initialization failed - browser may not support WebGPU");
                                                       rt.initState = InitState::Failed;
                                                       rt.initErrorMessage = "WebGPU initialization failed";
                                                       reportInitComplete(-1);
                                                       return;
                                                    }

                                                    printf("WasmBridge: WebGPU context ready, proceeding with remaining initialization\n");
                                                    rt.initState = InitState::WebGPUReady;
                                                    reportInitProgress("webgpu_ready", "GPU adapter and device acquired successfully");
                                                    rt.context->resize(rt.width, rt.height);

                                                    printf("WasmBridge: Initializing renderer...\n");
                                                    reportInitProgress("renderer_init", "Creating shader pipelines...");
                                                    // WGSL and pipeline errors are reported asynchronously, through
                                                    // error scopes; without them a bad shader leaves a black canvas.
                                                    rt.context->pushErrorScopes();
                                                    auto gpuRenderer = createWebGPURenderer(*rt.context);
                                                    const bool created = gpuRenderer->initialize();
                                                    rt.renderer = std::move(gpuRenderer);
                                                    rt.context->popErrorScopes([sampleRate, bufferSize, created, context](bool ok, const std::string& error)
                                                                               {
                                                                                  auto& st = runtimeState();
                                                                                  if (st.context.get() != context)
                                                                                     return;
                                                                                  if (!created || !ok)
                                                                                  {
                                                                                     printf("BespokeSynth WASM: WebGPU pipeline creation failed: %s\n", error.c_str());
                                                                                     st.renderer.reset();
                                                                                     st.initState = InitState::Failed;
                                                                                     st.initErrorMessage = "Shader pipeline creation failed";
                                                                                     if (!error.empty())
                                                                                        st.initErrorMessage += ": " + error;
                                                                                     reportInitProgress("renderer_failed", st.initErrorMessage.c_str());
                                                                                     reportInitComplete(-2);
                                                                                     return;
                                                                                  }

                                                                                  reportInitProgress("renderer_ready", "All shader pipelines compiled successfully");
                                                                                  st.initState = InitState::RendererReady;
                                                                                  if (!initializeAudioAndControls(sampleRate, bufferSize))
                                                                                  {
                                                                                     reportInitComplete(-3);
                                                                                     return;
                                                                                  }
                                                                                  reportInitComplete(0);
                                                                               });
                                                 });

   if (!started)
   {
      printf("BespokeSynth WASM: Failed to start WebGPU initialization\n");
      reportInitProgress("webgpu_start_failed", "Failed to start WebGPU initialization");
      state.initState = InitState::Failed;
      state.initErrorMessage = "Failed to start async WebGPU initialization";
      return -4;
   }

   return 1;
}

EMSCRIPTEN_KEEPALIVE int bespoke_reinit_renderer(int backend)
{
   auto& state = runtimeState();
   if (backend != 1 || state.initState != InitState::FullyInitialized)
      return -1;

   printf("BespokeSynth WASM: Re-initializing the renderer on WebGL2\n");
   // Renderers hold a reference to their context: release them first.
   state.renderer.reset();
   state.context.reset();
   state.webGL2Context.reset();
   state.rendererBackend = RendererBackendType::WebGL2;

   std::string error;
   if (!createWebGL2RendererOnCanvas(error))
   {
      state.initErrorMessage = error;
      printf("BespokeSynth WASM: Renderer re-initialization failed: %s\n", error.c_str());
      return -5;
   }
   return 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_process_events(void)
{
   if (runtimeState().context)
      runtimeState().context->processEvents();
}

EMSCRIPTEN_KEEPALIVE void bespoke_shutdown(void)
{
   auto& state = runtimeState();
   printf("BespokeSynth WASM: Shutting down\n");

   if (state.audioBackend)
   {
      state.audioBackend->stop();
      state.audioBackend->shutdown();
      state.audioBackend.reset();
   }

   for (int i = 0; i < 10 && state.audioCallbackActive.load(); ++i)
   {
   }

   state.knobs.clear();
   state.canvas.reset();
   state.audioGraphEngine.reset();
   resetScriptNoteQueues();
   state.renderer.reset();
   state.context.reset();
   state.webGL2Context.reset();

   state.initialized = false;
   state.initState = InitState::NotStarted;
   state.initErrorMessage.clear();

   printf("BespokeSynth WASM: Shutdown complete\n");
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_init_state(void)
{
   return static_cast<int>(runtimeState().initState);
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_init_error(void)
{
   return runtimeState().initErrorMessage.c_str();
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_fully_initialized(void)
{
   return (runtimeState().initState == InitState::FullyInitialized) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_theme_color(int colorId, float r, float g, float b, float a)
{
   auto id = static_cast<ThemeColorId>(colorId);
   runtimeState().theme.setColor(id, Color(r, g, b, a));
}

EMSCRIPTEN_KEEPALIVE void bespoke_reset_theme(void)
{
   runtimeState().theme.reset();
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_renderer_backend(int backend)
{
   auto& state = runtimeState();
   if (state.initState == InitState::NotStarted)
      state.rendererBackend = (backend == 1) ? RendererBackendType::WebGL2 : RendererBackendType::WebGPU;
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_font_mode(int mode)
{
   if (runtimeState().initState == InitState::NotStarted)
      setUiFontMode(mode == 1 ? UiFontMode::Pixel : UiFontMode::Sdf);
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_font_mode(void)
{
   return useSdfTextForFace(nullptr) ? 0 : 1;
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_webgl2_supported(void)
{
   return WebGL2Context::probeSupport("#canvas") ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_webgl2_error(void)
{
   return WebGL2Context::getLastError();
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_webgl_preserve_drawing_buffer(int enabled)
{
   auto& state = runtimeState();
   if (state.initState == InitState::NotStarted)
      state.webGLPreserveDrawingBuffer = enabled != 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_renderer_backend(void)
{
   return static_cast<int>(runtimeState().rendererBackend);
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_webgl_debug_mode(int mode)
{
   auto& state = runtimeState();
   state.webGLDebugMode = static_cast<WebGLDebugMode>(mode);
   if (state.renderer)
      state.renderer->setDebugMode(state.webGLDebugMode);
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_webgl_debug_mode(void)
{
   return static_cast<int>(runtimeState().webGLDebugMode);
}

} // extern "C"
