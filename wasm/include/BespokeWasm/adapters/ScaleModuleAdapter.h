/**
 * BespokeSynth WASM - Scale singleton adapter
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

      class ScaleModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "scale"; }
         const char* displayName() const override { return "Scale"; }
         ModuleCategory category() const override { return ModuleCategory::Other; }
         bool isSingleton() const override { return true; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::None; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;
      };

   } // namespace wasm
} // namespace bespoke
