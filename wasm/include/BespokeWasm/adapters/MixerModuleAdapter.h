/**
 * BespokeSynth WASM - Four-channel summing mixer adapter
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

      constexpr int kMixerChannelCount = 4;

      struct MixerParams
      {
         float level[kMixerChannelCount] = { 0.8f, 0.8f, 0.8f, 0.8f };
         float pan[kMixerChannelCount] = { 0.5f, 0.5f, 0.5f, 0.5f }; // 0 = L, 0.5 = centre, 1 = R
         float master = 1.0f;
      };

      /**
       * Each input port is its own channel (the engine does not pre-sum), so
       * per-channel level applies before the bus. The graph is mono today, so
       * there is no pan; add it when the output bus goes stereo.
       */
      class MixerModuleAdapter : public AdapterStorage<MixerParams>
      {
      public:
         const char* typeId() const override { return "mixer"; }
         const char* displayName() const override { return "Mixer"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
         bool sumsAudioInputs() const override { return false; }
         int outputChannelCount(const void*, const WasmAudioProcessContext&) const override { return 2; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
