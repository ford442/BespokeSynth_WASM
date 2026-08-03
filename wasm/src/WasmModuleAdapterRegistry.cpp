/**
 * BespokeSynth WASM - Module adapter registry and built-in adapters
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmModuleAdapter.h"
#include "BespokeWasm/adapters/AdsrModuleAdapter.h"
#include "BespokeWasm/adapters/DelayModuleAdapter.h"
#include "BespokeWasm/adapters/FilterModuleAdapter.h"
#include "BespokeWasm/adapters/NoiseModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <functional>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         class SimpleWasmModuleAdapter : public WasmModuleAdapter
         {
         public:
            SimpleWasmModuleAdapter(const char* typeId,
                                    const char* displayName,
                                    ModuleCategory category,
                                    WasmAudioRole audioRole,
                                    bool singleton,
                                    std::vector<WasmControlDescriptor> controls,
                                    std::function<std::unique_ptr<Module>(int)> factory,
                                    std::function<void(const std::map<std::string, float>&, AudioGraphNode&)> fillNode)
            : mTypeId(typeId)
            , mDisplayName(displayName)
            , mCategory(category)
            , mAudioRole(audioRole)
            , mSingleton(singleton)
            , mControls(std::move(controls))
            , mFactory(std::move(factory))
            , mFillNode(std::move(fillNode))
            {
            }

            const char* typeId() const override { return mTypeId; }
            const char* displayName() const override { return mDisplayName; }
            ModuleCategory category() const override { return mCategory; }
            bool isSingleton() const override { return mSingleton; }
            WasmAudioRole audioRole() const override { return mAudioRole; }

            std::vector<WasmControlDescriptor> controlDescriptors() const override { return mControls; }

            std::unique_ptr<Module> createUiModule(int id) const override
            {
               return mFactory(id);
            }

            void fillAudioGraphNode(const std::map<std::string, float>& controls,
                                    AudioGraphNode& node) const override
            {
               if (mFillNode)
                  mFillNode(controls, node);
            }

         private:
            const char* mTypeId;
            const char* mDisplayName;
            ModuleCategory mCategory;
            WasmAudioRole mAudioRole;
            bool mSingleton;
            std::vector<WasmControlDescriptor> mControls;
            std::function<std::unique_ptr<Module>(int)> mFactory;
            std::function<void(const std::map<std::string, float>&, AudioGraphNode&)> mFillNode;
         };

         float controlValue(const std::map<std::string, float>& controls, const char* name, float fallback)
         {
            auto it = controls.find(name);
            return it != controls.end() ? it->second : fallback;
         }
      }

      WasmModuleAdapterRegistry& WasmModuleAdapterRegistry::instance()
      {
         static WasmModuleAdapterRegistry registry;
         return registry;
      }

      WasmModuleAdapterRegistry::WasmModuleAdapterRegistry()
      {
         registerBuiltInAdapters();
      }

      void WasmModuleAdapterRegistry::registerAdapter(std::unique_ptr<WasmModuleAdapter> adapter)
      {
         if (!adapter)
            return;
         mTypes.push_back({ adapter->typeId(), adapter->displayName(), adapter->category() });
         mAdapters.push_back(std::move(adapter));
      }

      void WasmModuleAdapterRegistry::registerBuiltInAdapters()
      {
         registerAdapter(std::make_unique<SimpleWasmModuleAdapter>(
            "oscillator", "Oscillator", ModuleCategory::Synth, WasmAudioRole::AudioSource, false,
            std::vector<WasmControlDescriptor>{
               { "frequency", 440.0f }, { "volume", 0.7f }, { "waveform", 0.0f } },
            [](int id) { return std::make_unique<OscillatorModule>(id); },
            [](const std::map<std::string, float>& controls, AudioGraphNode& node)
            {
               node.frequency = controlValue(controls, "frequency", 440.0f);
               node.volume = controlValue(controls, "volume", 0.7f);
               node.waveform = static_cast<int>(controlValue(controls, "waveform", 0.0f));
            }));

         registerAdapter(std::make_unique<FilterModuleAdapter>());

         registerAdapter(std::make_unique<AdsrModuleAdapter>());
         registerAdapter(std::make_unique<DelayModuleAdapter>());
         registerAdapter(std::make_unique<NoiseModuleAdapter>());

         registerAdapter(std::make_unique<SimpleWasmModuleAdapter>(
            "gain", "Gain", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor, false,
            std::vector<WasmControlDescriptor>{ { "gain", 0.7f } },
            [](int id) { return std::make_unique<GainModule>(id); },
            [](const std::map<std::string, float>& controls, AudioGraphNode& node)
            {
               node.gain = controlValue(controls, "gain", 0.7f);
            }));

         registerAdapter(std::make_unique<SimpleWasmModuleAdapter>(
            "output", "Output", ModuleCategory::Other, WasmAudioRole::Sink, false,
            std::vector<WasmControlDescriptor>{ { "level", 0.0f } },
            [](int id) { return std::make_unique<OutputModule>(id); },
            [](const std::map<std::string, float>&, AudioGraphNode&) {}));

         registerAdapter(std::make_unique<SimpleWasmModuleAdapter>(
            "lfo", "LFO", ModuleCategory::Modulator, WasmAudioRole::ModulationSource, false,
            std::vector<WasmControlDescriptor>{
               { "rate", 1.0f }, { "depth", 1.0f }, { "shape", 0.0f } },
            [](int id) { return std::make_unique<LFOModule>(id); },
            [](const std::map<std::string, float>& controls, AudioGraphNode& node)
            {
               node.lfoRate = controlValue(controls, "rate", 1.0f);
               node.lfoDepth = controlValue(controls, "depth", 1.0f);
               node.lfoShape = static_cast<int>(controlValue(controls, "shape", 0.0f));
            }));

         registerAdapter(std::make_unique<SimpleWasmModuleAdapter>(
            "transport", "Transport", ModuleCategory::Other, WasmAudioRole::None, true,
            std::vector<WasmControlDescriptor>{ { "bpm", 120.0f }, { "swing", 0.0f } },
            [](int id) { return std::make_unique<TransportModule>(id); },
            [](const std::map<std::string, float>&, AudioGraphNode&) {}));

         registerAdapter(std::make_unique<SimpleWasmModuleAdapter>(
            "scale", "Scale", ModuleCategory::Other, WasmAudioRole::None, true,
            std::vector<WasmControlDescriptor>{ { "root", 0.0f }, { "type", 0.0f } },
            [](int id) { return std::make_unique<ScaleModule>(id); },
            [](const std::map<std::string, float>&, AudioGraphNode&) {}));
      }

      const WasmModuleAdapter* WasmModuleAdapterRegistry::find(const std::string& type) const
      {
         for (const auto& adapter : mAdapters)
         {
            if (adapter && adapter->typeId() == type)
               return adapter.get();
         }
         return nullptr;
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

   } // namespace wasm
} // namespace bespoke
