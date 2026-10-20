#pragma once

#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/Knob.h"
#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/Renderer2D.h"
#include "BespokeWasm/SDL2AudioBackend.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/WebGL2Context.h"
#include "BespokeWasm/WebGPUContext.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bespoke::wasm
{
   enum class InitState
   {
      NotStarted = 0,
      WebGPURequested = 1,
      WebGPUReady = 2,
      RendererReady = 3,
      AudioReady = 4,
      FullyInitialized = 5,
      WebGL2Requested = 6,
      WebGL2Ready = 7,
      Failed = -1
   };

   constexpr int kPanelCount = 3;

   enum class PanelType
   {
      Mixer = 0,
      Effects,
      Sequencer
   };

   struct PanelStatus
   {
      bool loaded = false;
      bool running = false;
      int frameCount = 0;
      float lastUpdateTime = 0.0f;
   };

   struct ControlInfoEntry
   {
      int id = 0;
      char type[16]{};
      char label[64]{};
      float value = 0.0f;
      float min = 0.0f;
      float max = 0.0f;
      char unit[16]{};
      float x = 0.0f;
      float y = 0.0f;
      float size = 0.0f;
   };

   // The sole owner of mutable WASM runtime state. Internal subsystems receive
   // this object through runtimeState(), avoiding cross-translation-unit globals.
   struct WasmRuntimeState
   {
      RendererBackendType rendererBackend = RendererBackendType::WebGPU;
      WebGLDebugMode webGLDebugMode = WebGLDebugMode::Normal;
      std::unique_ptr<WebGPUContext> context;
      std::unique_ptr<WebGL2Context> webGL2Context;
      std::unique_ptr<Renderer2D> renderer;
      std::unique_ptr<SDL2AudioBackend> audioBackend;
      std::unique_ptr<AudioGraphEngine> audioGraphEngine;
      std::unique_ptr<ModuleCanvas> canvas;
      std::vector<std::unique_ptr<Knob>> knobs;
      std::vector<ControlInfoEntry> controlInfoCache;
      std::string stateJsonBuffer;
      /** Backing store for the C strings the live-instrument getters return (valid until the next call). */
      std::string liveJsonBuffer;
      /** Backing store for bespoke_get_module_string. */
      std::string moduleStringBuffer;
      /** Hash of the sample the last import produced (bespoke_import_sample*). */
      std::string lastSampleHash;
      /** The last offline bounce as a WAV file, until the host frees it. */
      std::vector<uint8_t> offlineWav;
      /** Per-slot color overrides set from the host (bespoke_set_theme_color). */
      RuntimeTheme theme;
      /** Smoothed processing time / buffer duration of the SDL audio callback. */
      std::atomic<float> audioCpuLoad{ 0.0f };
      std::string stateErrorMessage;
      std::string initErrorMessage;
      PanelStatus panelStatus[kPanelCount];
      std::atomic<bool> audioCallbackActive{ false };
      /** When true, play/stop only toggles transport; JS owns the audio device (worklet). */
      bool externalAudioActive = false;
      float externalSampleRate = 44100.0f;
      /** The AudioWorklet engine (a second WASM instance) plays the graph; ours stays idle. */
      bool remoteAudioActive = false;
      /** Worklet engine telemetry the host mirrors in once per frame (bespoke_set_remote_audio_status). */
      double remoteBeat = 0.0;
      float remoteOutputPeak[2]{ 0.0f, 0.0f };
      int oscillatorModuleId = -1;
      int gainModuleId = -1;
      int viewMode = 0;
      bool legacyPanelsEnabled = false;
      int currentPanel = static_cast<int>(PanelType::Mixer);
      int width = 800;
      int height = 600;
      InitState initState = InitState::NotStarted;
      float renderTime = 0.0f;
      bool initialized = false;
      bool renderTestMode = false;
      int renderTestScene = 0; // 1 = canonical synth voice, 2 = drum bus (?renderTest=2)
      bool fontTestVisible = false;
      bool webGLPreserveDrawingBuffer = false;
   };

   WasmRuntimeState& runtimeState();

   /** Rate the audio graph is processing at (the worklet's when JS owns the device). */
   int currentDeviceSampleRate();
   /** Tell SampleStore the current rate so loads that don't know it (bundled kit) match. */
   void syncSampleStoreRate();
   /**
    * After a patch swap: restart the engine's clock and module runtime state (note-offs for notes still
    * sounding on the MIDI output are queued first) and size state for the new graph. The engine keeps its
    * prepared block size and its MIDI-out capture setting, so the patch is audible straight away.
    */
   void resetAudioGraphEngineForPatch();
}
