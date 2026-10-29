/**
 * BespokeSynth WASM - Waveshaper / saturation adapter implementation
 */

#include "BespokeWasm/adapters/WaveshaperModuleAdapter.h"
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
            { .id = 0, .name = "drive", .minValue = 1.0f, .maxValue = 20.0f, .defaultValue = 2.0f, .offset = offsetof(WaveshaperParams, drive), .flags = kParamClamp },
            { .id = 1, .name = "mode", .minValue = 0.0f, .maxValue = 3.0f, .defaultValue = static_cast<float>(kWaveshaperTanh), .kind = ParamKind::IntRound, .offset = offsetof(WaveshaperParams, mode), .flags = kParamClamp },
            { .id = 2, .name = "mix", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(WaveshaperParams, mix), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 3, .name = "level", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.7f, .offset = offsetof(WaveshaperParams, level), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" }, { PortType::Modulation, "Drive" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& waveshaperSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "drive", "Drive", SliderFormat::Decimal },
                                                                           { "mode", "Mode", SliderFormat::Choice, { "Soft", "Tanh", "Fold", "Hard" } },
                                                                           { "mix", "Mix", SliderFormat::Percent },
                                                                           { "level", "Level", SliderFormat::Percent },
                                                                           });
            return specs;
         }
      }

      float WaveshaperModuleAdapter::shape(float x, int mode)
      {
         switch (mode)
         {
            case kWaveshaperSoft:
               return x / (1.0f + std::fabs(x));
            case kWaveshaperFold:
            {
               // Reflect into [-1, 1]: period-4 triangle through the origin.
               const float t = std::fmod(std::fabs(x + 1.0f), 4.0f);
               return (t < 2.0f ? t : 4.0f - t) - 1.0f;
            }
            case kWaveshaperHard:
               return std::max(-1.0f, std::min(1.0f, x));
            case kWaveshaperTanh:
            default:
               return std::tanh(x);
         }
      }

      ParamTable WaveshaperModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> WaveshaperModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> WaveshaperModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> WaveshaperModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), waveshaperSpecs());
      }

      void WaveshaperModuleAdapter::processAudio(void* runtimeState,
                                                 const void* paramsPtr,
                                                 float* const* channels,
                                                 const WasmAudioProcessContext& context) const
      {
         (void)runtimeState;
         if (!paramsPtr || !channels)
            return;

         const auto& params = *static_cast<const WaveshaperParams*>(paramsPtr);
         const float dry = 1.0f - params.mix;
         for (int ch = 0; ch < context.numChannels; ++ch)
         {
            float* buffer = channels[ch];
            for (int i = 0; i < context.numSamples; ++i)
            {
               // Drive CV scales drive by (1 + cv): an LFO at depth 1 sweeps 0..2x.
               float drive = params.drive;
               if (context.modulationBuffer)
                  drive = std::max(0.0f, drive * (1.0f + context.modulationBuffer[i]));
               const float x = buffer[i];
               buffer[i] = (x * dry + shape(x * drive, params.mode) * params.mix) * params.level;
            }
         }
      }

      BESPOKE_REGISTER_MODULE(WaveshaperModuleAdapter);

   } // namespace wasm
} // namespace bespoke
