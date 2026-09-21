/**
 * BespokeSynth WASM - Waveshaper / saturation adapter implementation
 */

#include "BespokeWasm/adapters/WaveshaperModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         const std::vector<SliderSpec>& waveshaperSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "drive", "Drive", 1.0f, 20.0f, 2.0f, SliderFormat::Decimal },
               { "mode", "Mode", 0.0f, 3.0f, static_cast<float>(kWaveshaperTanh), SliderFormat::Choice, { "Soft", "Tanh", "Fold", "Hard" } },
               { "mix", "Mix", 0.0f, 1.0f, 1.0f, SliderFormat::Percent },
               { "level", "Level", 0.0f, 1.0f, 0.7f, SliderFormat::Percent },
            };
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

      std::vector<WasmControlDescriptor> WaveshaperModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(waveshaperSpecs());
      }

      std::vector<PortDescriptor> WaveshaperModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" }, { PortType::Modulation, "Drive" } };
      }

      std::vector<PortDescriptor> WaveshaperModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> WaveshaperModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), waveshaperSpecs());
      }

      void WaveshaperModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         const auto& specs = waveshaperSpecs();
         auto* params = new (dst) WaveshaperParams();
         params->drive = sliderSpecValue(specs, controls, "drive");
         params->mode = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "mode")));
         params->mix = sliderSpecValue(specs, controls, "mix");
         params->level = sliderSpecValue(specs, controls, "level");
      }

      void WaveshaperModuleAdapter::processAudio(void* runtimeState,
                                                 const void* paramsPtr,
                                                 float* buffer,
                                                 const WasmAudioProcessContext& context) const
      {
         (void)runtimeState;
         if (!paramsPtr || !buffer)
            return;

         const auto& params = *static_cast<const WaveshaperParams*>(paramsPtr);
         const float dry = 1.0f - params.mix;
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

      BESPOKE_REGISTER_MODULE(WaveshaperModuleAdapter);

   } // namespace wasm
} // namespace bespoke
