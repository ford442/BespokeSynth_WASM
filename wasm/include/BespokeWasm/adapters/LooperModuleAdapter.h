/**
 * BespokeSynth WASM - Transport-quantized looper adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/LooperArena.h"
#include "BespokeWasm/WasmModuleAdapter.h"

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

      class LooperModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "looper"; }
         const char* displayName() const override { return "Looper"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(LooperParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;
         void fillParams(int moduleId,
                         const WasmControlMap& controls,
                         const WasmStringMap& extras,
                         void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(LooperAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
