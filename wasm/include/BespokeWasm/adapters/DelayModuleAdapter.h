/**
 * BespokeSynth WASM - Delay effect adapter
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

      struct DelayParams
      {
         float time = 0.25f;
         float feedback = 0.35f;
         float mix = 0.35f;
      };

      /**
       * Followed in the runtime arena by two kMaxDelaySamples lines (L, R). The
       * lines are addressed from the state itself, not a stored pointer, so an
       * arena reallocation cannot leave them dangling.
       */
      struct DelayAdapterRuntimeState
      {
         size_t writeIndex = 0;
         size_t usedCapacity = 0;
         float lastSampleRate = 0.0f;
         int lastChannels = 1;
      };

      class DelayModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "delay"; }
         const char* displayName() const override { return "Delay"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(DelayParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override;
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         static constexpr float kMaxDelaySeconds = 2.0f;
         static constexpr size_t kMaxDelaySamples = 96000;
      };

   } // namespace wasm
} // namespace bespoke
