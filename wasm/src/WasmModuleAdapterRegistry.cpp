/**
 * BespokeSynth WASM - Module adapter registry
 *
 * Built-in adapters self-register via BESPOKE_REGISTER_MODULE in their translation units.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {
      WasmModuleAdapterRegistry& WasmModuleAdapterRegistry::instance()
      {
         static WasmModuleAdapterRegistry registry;
         return registry;
      }

      void WasmModuleAdapterRegistry::registerAdapter(std::unique_ptr<WasmModuleAdapter> adapter)
      {
         if (!adapter)
            return;
         const std::string typeId = adapter->typeId();
         if (mIndexByType.count(typeId))
            return;

         const int typeIndex = static_cast<int>(mAdapters.size());
         mTypes.push_back({ adapter->typeId(), adapter->displayName(), adapter->category() });
         mIndexByType.emplace(typeId, typeIndex);
         mAdapters.push_back(std::move(adapter));
      }

      const WasmModuleAdapter* WasmModuleAdapterRegistry::find(const std::string& type) const
      {
         return adapterAt(typeIndexFor(type));
      }

      const WasmModuleAdapter* WasmModuleAdapterRegistry::adapterAt(int typeIndex) const
      {
         if (typeIndex < 0 || typeIndex >= static_cast<int>(mAdapters.size()))
            return nullptr;
         return mAdapters[static_cast<size_t>(typeIndex)].get();
      }

      int WasmModuleAdapterRegistry::typeIndexFor(const std::string& type) const
      {
         auto it = mIndexByType.find(type);
         return it == mIndexByType.end() ? -1 : it->second;
      }

      std::vector<ModuleTypeInfo> WasmModuleAdapterRegistry::typesByCategory(ModuleCategory category) const
      {
         std::vector<ModuleTypeInfo> result;
         for (const auto& adapter : mAdapters)
         {
            if (!adapter || adapter->category() != category || adapter->isSingleton())
               continue;
            result.push_back({ adapter->typeId(), adapter->displayName(), adapter->category() });
         }
         return result;
      }

      std::vector<std::string> WasmModuleAdapterRegistry::serializableControlNames(const std::string& type) const
      {
         std::vector<std::string> names;
         if (const WasmModuleAdapter* adapter = find(type))
         {
            for (const auto& control : adapter->controlDescriptors())
               names.push_back(control.name);
         }
         return names;
      }

      std::unique_ptr<Module> WasmModuleAdapterRegistry::createModule(const std::string& type, int id) const
      {
         if (const WasmModuleAdapter* adapter = find(type))
            return adapter->createUiModule(id);
         return nullptr;
      }

      bool appendAudioGraphNode(AudioGraphSnapshot& snapshot,
                                int moduleId,
                                const std::string& type,
                                bool enabled,
                                const WasmControlMap& controls,
                                const WasmStringMap& extras)
      {
         auto& registry = WasmModuleAdapterRegistry::instance();
         const WasmModuleAdapter* adapter = registry.find(type);
         if (!adapter)
            return false;

         AudioGraphNode node;
         node.id = moduleId;
         node.typeIndex = registry.typeIndexFor(type);
         node.enabled = enabled;

         const uint32_t size = static_cast<uint32_t>(adapter->paramsSize());
         const uint32_t offset = alignAudioGraphOffset(static_cast<uint32_t>(snapshot.paramArena.size()));
         if (size > 0)
         {
            snapshot.paramArena.resize(static_cast<size_t>(offset) + size, 0);
            adapter->fillParams(moduleId, controls, extras, snapshot.paramArena.data() + offset);
         }
         node.paramOffset = offset;
         node.paramSize = size;
         snapshot.nodes.push_back(node);
         return true;
      }

   } // namespace wasm
} // namespace bespoke
