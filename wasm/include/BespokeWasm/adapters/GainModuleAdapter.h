/**
 * BespokeSynth WASM - Gain adapter
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

      struct GainParams
      {
         float gain = 0.7f;
      };

      class GainModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "gain"; }
         const char* displayName() const override { return "Gain"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(GainParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
