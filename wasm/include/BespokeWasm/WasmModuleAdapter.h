/**
 * BespokeSynth WASM - Module adapter boundary (ADR: docs/wasm-module-porting.md)
 *
 * Adapters connect canvas UI modules, serialization, and audio graph processing.
 * Each adapter owns a POD parameter block, runtime state, and port layout.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/Module.h"
#include "BespokeWasm/ModuleTypes.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class Module;

      using WasmControlMap = std::map<std::string, float>;

      struct WasmControlDescriptor
      {
         std::string name;
         float defaultValue = 0.0f;
      };

      struct PortDescriptor
      {
         PortType type = PortType::Audio;
         const char* name = "";
      };

      struct WasmNoteEvent
      {
         int pitch = 60;
         float velocity = 0.0f;
         bool isNoteOn = false;
      };

      struct WasmAudioProcessContext
      {
         float sampleRate = 44100.0f;
         int numSamples = 0;
         double blockStartTimeSeconds = 0.0;
         const float* modulationBuffer = nullptr;
         const WasmNoteEvent* notes = nullptr;
         int noteCount = 0;
         bool hasNoteCable = false;
      };

      inline float wasmControlValue(const WasmControlMap& controls, const char* name, float fallback)
      {
         auto it = controls.find(name);
         return it != controls.end() ? it->second : fallback;
      }

      class WasmModuleAdapter
      {
      public:
         virtual ~WasmModuleAdapter() = default;

         virtual const char* typeId() const = 0;
         virtual const char* displayName() const = 0;
         virtual ModuleCategory category() const = 0;
         virtual bool isSingleton() const { return false; }
         virtual WasmAudioRole audioRole() const { return WasmAudioRole::None; }

         virtual std::vector<WasmControlDescriptor> controlDescriptors() const = 0;
         virtual std::vector<PortDescriptor> inputPorts() const { return {}; }
         virtual std::vector<PortDescriptor> outputPorts() const { return {}; }
         virtual std::unique_ptr<Module> createUiModule(int id) const = 0;

         virtual size_t paramsSize() const { return 0; }
         virtual void fillParams(const WasmControlMap& controls, void* dst) const
         {
            (void)controls;
            (void)dst;
         }

         virtual size_t runtimeStateSize() const { return 0; }
         virtual void initRuntimeState(void* runtimeState) const { (void)runtimeState; }
         virtual void destroyRuntimeState(void* runtimeState) const { (void)runtimeState; }

         virtual void processAudio(void* runtimeState,
                                   const void* params,
                                   float* buffer,
                                   const WasmAudioProcessContext& context) const
         {
            (void)runtimeState;
            (void)params;
            (void)buffer;
            (void)context;
         }

         virtual void emitNotesForBeatRange(void* runtimeState,
                                            const void* params,
                                            double beatStart,
                                            double beatEnd,
                                            WasmNoteEvent* outNotes,
                                            int maxNotes,
                                            int& outCount) const
         {
            (void)runtimeState;
            (void)params;
            (void)beatStart;
            (void)beatEnd;
            (void)outNotes;
            (void)maxNotes;
            outCount = 0;
         }
      };

      class WasmModuleAdapterRegistry
      {
      public:
         static WasmModuleAdapterRegistry& instance();

         void registerAdapter(std::unique_ptr<WasmModuleAdapter> adapter);
         const WasmModuleAdapter* find(const std::string& type) const;
         const WasmModuleAdapter* adapterAt(int typeIndex) const;
         int typeIndexFor(const std::string& type) const;
         const std::vector<ModuleTypeInfo>& registeredTypes() const { return mTypes; }
         std::vector<ModuleTypeInfo> typesByCategory(ModuleCategory category) const;
         std::vector<std::string> serializableControlNames(const std::string& type) const;

         std::unique_ptr<Module> createModule(const std::string& type, int id) const;

      private:
         WasmModuleAdapterRegistry() = default;

         std::vector<std::unique_ptr<WasmModuleAdapter>> mAdapters;
         std::vector<ModuleTypeInfo> mTypes;
         std::unordered_map<std::string, int> mIndexByType;
      };

      class WasmModuleAdapterRegistrar
      {
      public:
         using Factory = std::unique_ptr<WasmModuleAdapter> (*)();

         explicit WasmModuleAdapterRegistrar(Factory factory)
         {
            if (factory)
               WasmModuleAdapterRegistry::instance().registerAdapter(factory());
         }
      };

      bool appendAudioGraphNode(AudioGraphSnapshot& snapshot,
                                int moduleId,
                                const std::string& type,
                                bool enabled,
                                const WasmControlMap& controls);

#define BESPOKE_REGISTER_MODULE(AdapterClass)                                    \
   static bespoke::wasm::WasmModuleAdapterRegistrar g_bespoke_register_##AdapterClass( \
      []() -> std::unique_ptr<bespoke::wasm::WasmModuleAdapter>                  \
      {                                                                          \
         return std::make_unique<AdapterClass>();                                \
      })

   } // namespace wasm
} // namespace bespoke
