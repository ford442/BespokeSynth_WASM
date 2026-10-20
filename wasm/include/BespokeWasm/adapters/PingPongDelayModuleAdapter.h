/**
 * BespokeSynth WASM - Tempo-synced ping-pong delay adapter
 *
 * Echoes alternate between the two sides: the input enters the left line, each left
 * echo feeds the right line, and each right echo comes back to the left, losing
 * `feedback` on every hop, through a low-pass "tone" filter. The time follows the
 * transport (a note interval at the live BPM) unless the interval is "ms". Always
 * stereo out; `width` pulls the two taps toward the centre. The plain `delay`
 * adapter keeps its free-running ping-pong switch; this one is the synced, darker
 * dub-style variant.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {

      struct PingPongDelayParams
      {
         int interval = 3; // index into PingPongDelayModuleAdapter::intervalBeats; 0 = timeMs
         float timeMs = 375.0f;
         float feedback = 0.45f;
         float mix = 0.35f;
         float tone = 6000.0f; // feedback low-pass, Hz
         float width = 1.0f;
      };

      struct PingPongDelayRuntimeState
      {
         static constexpr size_t kMaxSamples = 96000; // 2 s at 48 kHz

         size_t writeIndex = 0;
         float toneState[2]{};
         float lines[2][kMaxSamples]{};
      };

      class PingPongDelayModuleAdapter : public AdapterStorage<PingPongDelayParams, PingPongDelayRuntimeState>
      {
      public:
         const char* typeId() const override { return "pingpongdelay"; }
         const char* displayName() const override { return "Ping-Pong Delay"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override;

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         /** Beats per echo for `interval` (0 = free time, returns 0). */
         static double intervalBeats(int interval);
         /** Echo spacing in samples, clamped to the line. */
         static int delaySamples(const PingPongDelayParams& params, float bpm, float sampleRate);

         static constexpr int kIntervalCount = 8;
      };

   } // namespace wasm
} // namespace bespoke
