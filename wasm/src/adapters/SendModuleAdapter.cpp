/**
 * BespokeSynth WASM - Audio send adapter implementation
 */

#include "BespokeWasm/adapters/SendModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "amount", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(SendParams, amount), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "crossfade", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Bool, .offset = offsetof(SendParams, crossfade) },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" }, { PortType::Audio, "Send" } };

         const std::vector<SliderSpec>& sendSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "amount", "Amount", SliderFormat::Percent },
                                                                           { "crossfade", "Crossfade", SliderFormat::Choice, { "off", "on" } },
                                                                           });
            return specs;
         }
      }

      ParamTable SendModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> SendModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> SendModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> SendModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), sendSpecs(), 140.0f);
      }

      void SendModuleAdapter::processAudio(void* runtimeState,
                                           const void* paramsPtr,
                                           float* const* channels,
                                           const WasmAudioProcessContext& context) const
      {
         (void)runtimeState;
         if (!paramsPtr || !channels)
            return;
         const auto& params = paramsOf(paramsPtr);
         for (int ch = 0; ch < context.numChannels; ++ch)
         {
            if (context.auxOutputCount > 0 && context.auxOutputs[ch])
               dsp::accumulateScaled(context.auxOutputs[ch], channels[ch], context.numSamples, params.amount);
            if (params.crossfade)
               dsp::scale(channels[ch], context.numSamples, 1.0f - params.amount);
         }
      }

      BESPOKE_REGISTER_MODULE(SendModuleAdapter);

   } // namespace wasm
} // namespace bespoke
