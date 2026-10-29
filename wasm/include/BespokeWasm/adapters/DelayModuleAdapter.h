/**
 * BespokeSynth WASM - Delay effect adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"

namespace bespoke
{
   namespace wasm
   {

      struct DelayParams
      {
         float time = 0.25f;
         float feedback = 0.35f;
         float mix = 0.35f;
         /** 0 = independent L/R lines; 1 = echoes alternate sides (input enters on the left). */
         float pingpong = 0.0f;
      };

      /**
       * Holds the two kMaxSamples lines (L, R) inline. The lines are addressed
       * from the state itself, not a stored pointer, so an arena reallocation
       * cannot leave them dangling.
       */
      struct DelayAdapterRuntimeState
      {
         static constexpr size_t kMaxSamples = 96000;

         size_t writeIndex = 0;
         size_t usedCapacity = kMaxSamples;
         float lastSampleRate = 0.0f;
         int lastChannels = 1;
         float lines[2][kMaxSamples]{};
      };

      class DelayModuleAdapter : public AdapterStorage<DelayParams, DelayAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "delay"; }
         const char* displayName() const override { return "Delay"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;
         /** Stereo for a stereo input, or whenever ping-pong is on. */
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         static constexpr float kMaxDelaySeconds = 2.0f;
         static constexpr size_t kMaxDelaySamples = DelayAdapterRuntimeState::kMaxSamples;
      };

   } // namespace wasm
} // namespace bespoke
