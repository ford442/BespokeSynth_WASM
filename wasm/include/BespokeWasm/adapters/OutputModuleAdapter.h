/**
 * BespokeSynth WASM - Output sink adapter
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

      class OutputModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "output"; }
         const char* displayName() const override { return "Output"; }
         ModuleCategory category() const override { return ModuleCategory::Other; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::Sink; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;
      };

   } // namespace wasm
} // namespace bespoke
