#pragma once

/**
 * Shared helpers for WasmBridge*.cpp translation units. Not part of the C ABI.
 */

#include "BespokeWasm/WasmRuntimeState.h"

#include <atomic>
#include <cstdint>
#include <emscripten.h>

namespace bespoke
{
   namespace wasm
   {
      constexpr const char* kWasmVersion = "1.0.0-wasm";

      extern std::atomic<uint32_t> gHostRenderCallCount;
      extern std::atomic<uint32_t> gHostMouseDownCallCount;
      extern float gFrameDeltaSeconds;

      void reportInitProgress(const char* step, const char* detail);
      void reportInitComplete(int status);
      void wasmAudioCallback(const float* const* input, float* const* output,
                             int numInputChannels, int numOutputChannels, int numSamples);
      bool initializeAudioAndControls(int sampleRate, int bufferSize);

      void ensureLegacyDemoKnobs();
      void renderDemoPanels();
      const char* legacyPanelName(int panelIndex);
      void logLegacyPanelStatus(int panelIndex, const char* action);

   } // namespace wasm
} // namespace bespoke
