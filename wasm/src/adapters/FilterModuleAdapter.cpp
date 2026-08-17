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
#include <new>

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

      std::vector<PortDescriptor> FilterModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" }, { PortType::Modulation, "CV" } };
      }

      std::vector<PortDescriptor> FilterModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> FilterModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<FilterModule>(id);
      }

      void FilterModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) FilterParams();
         params->cutoff = wasmControlValue(controls, "cutoff", 1000.0f);
         params->resonance = wasmControlValue(controls, "resonance", 0.5f);
         params->type = static_cast<int>(wasmControlValue(controls, "type", 0.0f));
      }

      void FilterModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) FilterAdapterRuntimeState();
      }

      void FilterModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<FilterAdapterRuntimeState*>(runtimeState)->~FilterAdapterRuntimeState();
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
                                             const void* paramsPtr,
                                             float* buffer,
                                             const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<FilterAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const FilterParams*>(paramsPtr);
         if (state.lastSampleRate != context.sampleRate)
         {
            state.filter.SetSampleRate(context.sampleRate);
            state.filter.Clear();
            state.lastSampleRate = context.sampleRate;
         }

         const FilterType filterType = filterTypeFor(params.type);
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

            const float cutoff = clampFloat(params.cutoff * std::pow(2.0f, modulation * 2.0f),
                                            20.0f, context.sampleRate * 0.45f);
            const float q = clampFloat(0.2f + params.resonance * 9.8f, 0.2f, 10.0f);
            state.filter.SetFilterParams(cutoff, q);
            buffer[i] = state.filter.Filter(buffer[i]);
         }
      }

      BESPOKE_REGISTER_MODULE(FilterModuleAdapter);

   } // namespace wasm
} // namespace bespoke
