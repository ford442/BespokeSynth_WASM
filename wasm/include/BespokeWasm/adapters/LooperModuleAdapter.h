/**
 * BespokeSynth WASM - Transport-quantized looper adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include "BespokeWasm/LooperArena.h"

namespace bespoke
{
   namespace wasm
   {

      struct LooperParams
      {
         LooperArena* arena = nullptr;
         float mix = 1.0f;
         int bars = 1;
         int command = 0; // 0 none, 1 record, 2 overdub, 3 play, 4 stop
      };

      struct LooperAdapterRuntimeState
      {
         int lastCommand = 0;
      };

      class LooperModuleAdapter : public AdapterStorage<LooperParams, LooperAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "looper"; }
         const char* displayName() const override { return "Looper"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
         /** Widest of the live input and the recorded loop. */
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override;

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void bindParams(int moduleId, const WasmStringMap& extras, void* params) const override;
         void rebindParams(int moduleId, void* params) const override;
         void collectResources(int moduleId, const void* params, WasmResourceRefs& refs) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
