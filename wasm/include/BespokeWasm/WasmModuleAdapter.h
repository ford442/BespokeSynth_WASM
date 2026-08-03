/**
 * BespokeSynth WASM - Module adapter boundary (ADR: docs/wasm-module-porting.md)
 *
 * Adapters connect canvas UI modules, serialization, and audio graph processing.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/Module.h"
#include "BespokeWasm/ModuleTypes.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class Module;

      struct WasmControlDescriptor
      {
         std::string name;
         float defaultValue = 0.0f;
      };

      enum class WasmAudioRole
      {
         None,
         AudioSource,
         AudioProcessor,
         ModulationSource,
         NoteSource,
         Sink
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
         std::function<float(int sampleIndex)> modulationAt;
         const WasmNoteEvent* notes = nullptr;
         int noteCount = 0;
      };

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
         virtual std::unique_ptr<Module> createUiModule(int id) const = 0;

         virtual void fillAudioGraphNode(const std::map<std::string, float>& controls,
                                         AudioGraphNode& node) const = 0;

         virtual void processAudio(void* runtimeState,
                                   const AudioGraphNode& node,
                                   float* buffer,
                                   const WasmAudioProcessContext& context) const
         {
            (void)runtimeState;
            (void)node;
            (void)buffer;
            (void)context;
         }

         virtual size_t runtimeStateSize() const { return 0; }
         virtual void initRuntimeState(void* runtimeState) const { (void)runtimeState; }
      };

      class WasmModuleAdapterRegistry
      {
      public:
         static WasmModuleAdapterRegistry& instance();

         void registerAdapter(std::unique_ptr<WasmModuleAdapter> adapter);
         const WasmModuleAdapter* find(const std::string& type) const;
         const std::vector<ModuleTypeInfo>& registeredTypes() const { return mTypes; }
         std::vector<ModuleTypeInfo> typesByCategory(ModuleCategory category) const;
         std::vector<std::string> serializableControlNames(const std::string& type) const;

         std::unique_ptr<Module> createModule(const std::string& type, int id) const;

      private:
         WasmModuleAdapterRegistry();
         void registerBuiltInAdapters();

         std::vector<std::unique_ptr<WasmModuleAdapter>> mAdapters;
         std::vector<ModuleTypeInfo> mTypes;
      };

   } // namespace wasm
} // namespace bespoke
