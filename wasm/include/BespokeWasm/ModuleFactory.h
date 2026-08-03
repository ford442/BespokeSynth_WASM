/**
 * BespokeSynth WASM - Module factory (registry-backed)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/Module.h"
#include "BespokeWasm/ModuleTypes.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <memory>
#include <string>

namespace bespoke
{
   namespace wasm
   {

      class Module;

      class ModuleFactory
      {
      public:
         static ModuleFactory& instance();

         std::unique_ptr<Module> createModule(const std::string& type, int id);
         const std::vector<ModuleTypeInfo>& getRegisteredTypes() const;
         std::vector<ModuleTypeInfo> getTypesByCategory(ModuleCategory category) const;

      private:
         ModuleFactory() = default;
      };

   } // namespace wasm
} // namespace bespoke
