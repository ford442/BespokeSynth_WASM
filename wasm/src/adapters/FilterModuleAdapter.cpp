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
#include <cstddef>

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

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "cutoff", .minValue = 20.0f, .maxValue = 20000.0f, .defaultValue = 1000.0f, .offset = offsetof(FilterParams, cutoff), .smoothingMs = kParamSmoothingMs },
            { .id = 1, .name = "resonance", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(FilterParams, resonance) },
            { .id = 2, .name = "type", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f, .kind = ParamKind::Int, .offset = offsetof(FilterParams, type) },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" }, { PortType::Modulation, "CV" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };
      }

      ParamTable FilterModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> FilterModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> FilterModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> FilterModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<FilterModule>(id);
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
                                             float* const* channels,
                                             const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;

         auto& state = *static_cast<FilterAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const FilterParams*>(paramsPtr);
         if (state.lastSampleRate != context.sampleRate)
         {
            for (auto& filter : state.filter)
            {
               filter.SetSampleRate(context.sampleRate);
               filter.Clear();
            }
            state.lastSampleRate = context.sampleRate;
         }

         const FilterType filterType = filterTypeFor(params.type);
         if (state.lastFilterType != static_cast<int>(filterType))
         {
            for (auto& filter : state.filter)
               filter.SetFilterType(filterType);
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
            for (int ch = 0; ch < context.numChannels; ++ch)
            {
               state.filter[ch].SetFilterParams(cutoff, q);
               channels[ch][i] = state.filter[ch].Filter(channels[ch][i]);
            }
         }
      }

      BESPOKE_REGISTER_MODULE(FilterModuleAdapter);

   } // namespace wasm
} // namespace bespoke
