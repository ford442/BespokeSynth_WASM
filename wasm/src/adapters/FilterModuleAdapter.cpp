/**
 * BespokeSynth WASM - Filter module adapter implementation
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/adapters/FilterModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }
      }

      std::vector<WasmControlDescriptor> FilterModuleAdapter::controlDescriptors() const
      {
         return {
            { "cutoff", 1000.0f },
            { "resonance", 0.5f },
            { "type", 0.0f },
         };
      }

      std::unique_ptr<Module> FilterModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<FilterModule>(id);
      }

      void FilterModuleAdapter::fillAudioGraphNode(const std::map<std::string, float>& controls,
                                                   AudioGraphNode& node) const
      {
         auto get = [&](const char* name, float fallback) -> float
         {
            auto it = controls.find(name);
            return it != controls.end() ? it->second : fallback;
         };
         node.cutoff = get("cutoff", 1000.0f);
         node.resonance = get("resonance", 0.5f);
         node.filterType = static_cast<int>(get("type", 0.0f));
      }

      void FilterModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         std::memset(runtimeState, 0, sizeof(FilterAdapterRuntimeState));
      }

      FilterType FilterModuleAdapter::filterTypeFor(int filterType)
      {
         switch (filterType % 3)
         {
            case 1:
               return kFilterType_Highpass;
            case 2:
               return kFilterType_Bandpass;
            default:
               return kFilterType_Lowpass;
         }
      }

      void FilterModuleAdapter::processAudio(void* runtimeState,
                                             const AudioGraphNode& node,
                                             float* buffer,
                                             const WasmAudioProcessContext& context) const
      {
         auto& state = *static_cast<FilterAdapterRuntimeState*>(runtimeState);
         if (state.lastSampleRate != context.sampleRate)
         {
            state.filter.SetSampleRate(context.sampleRate);
            state.filter.Clear();
            state.lastSampleRate = context.sampleRate;
         }

         const FilterType filterType = filterTypeFor(node.filterType);
         if (state.lastFilterType != static_cast<int>(filterType))
         {
            state.filter.SetFilterType(filterType);
            state.lastFilterType = static_cast<int>(filterType);
         }

         for (int i = 0; i < context.numSamples; ++i)
         {
            float modulation = 0.0f;
            if (context.modulationBuffer)
               modulation = context.modulationBuffer[i];

            const float cutoff = clampFloat(node.cutoff * std::pow(2.0f, modulation * 2.0f),
                                            20.0f, context.sampleRate * 0.45f);
            const float q = clampFloat(0.2f + node.resonance * 9.8f, 0.2f, 10.0f);
            state.filter.SetFilterParams(cutoff, q);
            buffer[i] = state.filter.Filter(buffer[i]);
         }
      }

   } // namespace wasm
} // namespace bespoke
