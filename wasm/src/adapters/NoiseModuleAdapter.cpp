/**
 * BespokeSynth WASM - Noise source adapter implementation
 */

#include "BespokeWasm/adapters/NoiseModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
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

         float nextWhite(NoiseAdapterRuntimeState& state)
         {
            state.rng = state.rng * 1664525u + 1013904223u;
            const float unit = static_cast<float>(state.rng >> 8) * (1.0f / 16777216.0f);
            return unit * 2.0f - 1.0f;
         }

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "volume", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.35f, .offset = offsetof(NoiseParams, volume), .smoothingMs = kParamSmoothingMs },
            { .id = 1, .name = "color", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Int, .offset = offsetof(NoiseParams, color) },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };
      }

      ParamTable NoiseModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> NoiseModuleAdapter::inputPorts() const
      {
         return {};
      }

      std::span<const PortDescriptor> NoiseModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> NoiseModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<NoiseModule>(id);
      }

      void NoiseModuleAdapter::processAudio(void* runtimeState,
                                            const void* paramsPtr,
                                            float* const* channels,
                                            const WasmAudioProcessContext& context) const
      {
         float* buffer = channels ? channels[0] : nullptr;
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<NoiseAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const NoiseParams*>(paramsPtr);
         const float volume = clampFloat(params.volume, 0.0f, 1.0f);
         const bool pink = (params.color % 2) == 1;

         for (int i = 0; i < context.numSamples; ++i)
         {
            float sample = nextWhite(state);
            if (pink)
            {
               // Paul Kellet approximate pink filter
               state.pinkB0 = 0.99765f * state.pinkB0 + sample * 0.0990460f;
               state.pinkB1 = 0.96300f * state.pinkB1 + sample * 0.2965164f;
               state.pinkB2 = 0.57000f * state.pinkB2 + sample * 1.0526913f;
               sample = state.pinkB0 + state.pinkB1 + state.pinkB2 + sample * 0.1848f;
               sample *= 0.05f;
            }
            buffer[i] = sample * volume;
         }
      }

      BESPOKE_REGISTER_MODULE(NoiseModuleAdapter);

   } // namespace wasm
} // namespace bespoke
