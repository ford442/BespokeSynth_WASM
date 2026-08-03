/**
 * BespokeSynth WASM - Delay effect adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/WasmModuleAdapter.h"
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      struct DelayAdapterRuntimeState
      {
         std::vector<float> buffer;
         size_t writeIndex = 0;
         float lastSampleRate = 0.0f;
      };

      class DelayModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "delay"; }
         const char* displayName() const override { return "Delay"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;
         void fillAudioGraphNode(const std::map<std::string, float>& controls,
                                 AudioGraphNode& node) const override;

         size_t runtimeStateSize() const override { return sizeof(DelayAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const AudioGraphNode& node,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;

         static constexpr float kMaxDelaySeconds = 2.0f;
      };

   } // namespace wasm
} // namespace bespoke
