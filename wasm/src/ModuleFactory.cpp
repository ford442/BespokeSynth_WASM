/**
 * BespokeSynth WASM
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleFactory.h"
#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {
      ModuleFactory& ModuleFactory::instance()
      {
         static ModuleFactory factory;
         return factory;
      }

      std::unique_ptr<Module> ModuleFactory::createModule(const std::string& type, int id)
      {
         return WasmModuleAdapterRegistry::instance().createModule(type, id);
      }

      const std::vector<ModuleTypeInfo>& ModuleFactory::getRegisteredTypes() const
      {
         return WasmModuleAdapterRegistry::instance().registeredTypes();
      }

      std::vector<ModuleTypeInfo> ModuleFactory::getTypesByCategory(ModuleCategory category) const
      {
         return WasmModuleAdapterRegistry::instance().typesByCategory(category);
      }
   }
}
