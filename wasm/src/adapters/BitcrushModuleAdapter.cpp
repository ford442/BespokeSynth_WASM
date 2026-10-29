/**
 * BespokeSynth WASM - Bitcrush adapter implementation
 */

#include "BespokeWasm/adapters/BitcrushModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "crush", .minValue = 1.0f, .maxValue = 24.0f, .defaultValue = 1.0f, .offset = offsetof(BitcrushParams, crush), .flags = kParamClamp },
            { .id = 1, .name = "downsamp", .minValue = 1.0f, .maxValue = 40.0f, .defaultValue = 1.0f, .offset = offsetof(BitcrushParams, downsample), .flags = kParamClamp },
            { .id = 2, .name = "mix", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(BitcrushParams, mix), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& bitcrushSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "crush", "Crush", SliderFormat::Decimal },
                                                                           { "downsamp", "Downsample", SliderFormat::Integer },
                                                                           { "mix", "Mix", SliderFormat::Percent },
                                                                           });
            return specs;
         }
      }

      ParamTable BitcrushModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> BitcrushModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> BitcrushModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> BitcrushModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), bitcrushSpecs(), 160.0f);
      }

      void BitcrushModuleAdapter::processAudio(void* runtimeState,
                                               const void* paramsPtr,
                                               float* const* channels,
                                               const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;
         auto& state = stateOf(runtimeState);
         const auto& params = paramsOf(paramsPtr);
         // Desktop: 2^(25 - crush) levels, so crush 1 is 24-bit and 24 leaves two levels.
         const float levels = std::exp2(25.0f - std::max(1.0f, std::min(24.0f, params.crush)));
         const float invLevels = 1.0f / levels;
         const int hold = std::max(1, static_cast<int>(params.downsample));
         const float wet = params.mix;
         const float dry = 1.0f - wet;
         for (int ch = 0; ch < context.numChannels && ch < 2; ++ch)
         {
            float* buffer = channels[ch];
            for (int i = 0; i < context.numSamples; ++i)
            {
               // Sample on the first frame of every `hold`-frame group, then repeat it.
               if (state.counter[ch] == 0)
                  state.held[ch] = buffer[i];
               state.counter[ch] = state.counter[ch] + 1 >= hold ? 0 : state.counter[ch] + 1;
               const float crushed = std::trunc(state.held[ch] * levels) * invLevels;
               buffer[i] = buffer[i] * dry + crushed * wet;
            }
         }
      }

      BESPOKE_REGISTER_MODULE(BitcrushModuleAdapter);

   } // namespace wasm
} // namespace bespoke
