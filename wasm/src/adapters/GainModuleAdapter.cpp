/**
 * BespokeSynth WASM - Gain adapter implementation
 */

#include "BespokeWasm/adapters/GainModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
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

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "gain", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.7f, .offset = offsetof(GainParams, gain), .smoothingMs = kParamSmoothingMs },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" }, { PortType::Modulation, "Mod" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };
      }

      ParamTable GainModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> GainModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> GainModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> GainModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<GainModule>(id);
      }

      void GainModuleAdapter::processAudio(void* runtimeState,
                                           const void* paramsPtr,
                                           float* const* channels,
                                           const WasmAudioProcessContext& context) const
      {
         (void)runtimeState;
         if (!paramsPtr || !channels)
            return;

         const auto& params = *static_cast<const GainParams*>(paramsPtr);
         const float gain = clampFloat(params.gain, 0.0f, 1.0f);
         if (!context.modulationBuffer)
         {
            for (int ch = 0; ch < context.numChannels; ++ch)
               dsp::scale(channels[ch], context.numSamples, gain);
            return;
         }
         // Mod adds to the gain (an envelope at gain 0 is a VCA, an LFO at 0.5 a tremolo).
         for (int i = 0; i < context.numSamples; ++i)
         {
            const float modulated = clampFloat(gain + context.modulationBuffer[i], 0.0f, 1.0f);
            for (int ch = 0; ch < context.numChannels; ++ch)
               channels[ch][i] *= modulated;
         }
      }

      BESPOKE_REGISTER_MODULE(GainModuleAdapter);

   } // namespace wasm
} // namespace bespoke
