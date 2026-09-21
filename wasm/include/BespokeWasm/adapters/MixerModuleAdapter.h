/**
 * BespokeSynth WASM - Four-channel summing mixer adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      constexpr int kMixerChannelCount = 4;

      struct MixerParams
      {
         float level[kMixerChannelCount] = { 0.8f, 0.8f, 0.8f, 0.8f };
         float master = 1.0f;
      };

      /**
       * Each input port is its own channel (the engine does not pre-sum), so
       * per-channel level applies before the bus. The graph is mono today, so
       * there is no pan; add it when the output bus goes stereo.
       */
      class MixerModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "mixer"; }
         const char* displayName() const override { return "Mixer"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
         bool sumsAudioInputs() const override { return false; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(MixerParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
