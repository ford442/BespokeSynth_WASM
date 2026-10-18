/**
 * BespokeSynth WASM - Four-channel summing mixer adapter implementation
 */

#include "BespokeWasm/adapters/MixerModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
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
         const char* const kLevelNames[kMixerChannelCount] = { "level1", "level2", "level3", "level4" };
         const char* const kPanNames[kMixerChannelCount] = { "pan1", "pan2", "pan3", "pan4" };
         constexpr float kHalfPi = 1.57079632679489661923f;
         constexpr float kSqrt2 = 1.41421356237309504880f;

         const std::vector<SliderSpec>& mixerSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "level1", "Ch 1", 0.0f, 1.0f, 0.8f, SliderFormat::Percent },
               { "level2", "Ch 2", 0.0f, 1.0f, 0.8f, SliderFormat::Percent },
               { "level3", "Ch 3", 0.0f, 1.0f, 0.8f, SliderFormat::Percent },
               { "level4", "Ch 4", 0.0f, 1.0f, 0.8f, SliderFormat::Percent },
               { "pan1", "Pan 1", 0.0f, 1.0f, 0.5f, SliderFormat::Pan },
               { "pan2", "Pan 2", 0.0f, 1.0f, 0.5f, SliderFormat::Pan },
               { "pan3", "Pan 3", 0.0f, 1.0f, 0.5f, SliderFormat::Pan },
               { "pan4", "Pan 4", 0.0f, 1.0f, 0.5f, SliderFormat::Pan },
               { "master", "Master", 0.0f, 1.0f, 1.0f, SliderFormat::Percent },
            };
            return specs;
         }
      }

      std::vector<WasmControlDescriptor> MixerModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(mixerSpecs());
      }

      std::vector<PortDescriptor> MixerModuleAdapter::inputPorts() const
      {
         return {
            { PortType::Audio, "In 1" },
            { PortType::Audio, "In 2" },
            { PortType::Audio, "In 3" },
            { PortType::Audio, "In 4" },
         };
      }

      std::vector<PortDescriptor> MixerModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> MixerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), mixerSpecs(), 150.0f);
      }

      void MixerModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) MixerParams();
         for (int ch = 0; ch < kMixerChannelCount; ++ch)
         {
            params->level[ch] = sliderSpecValue(mixerSpecs(), controls, kLevelNames[ch]);
            params->pan[ch] = sliderSpecValue(mixerSpecs(), controls, kPanNames[ch]);
         }
         params->master = sliderSpecValue(mixerSpecs(), controls, "master");
      }

      void MixerModuleAdapter::processAudio(void* runtimeState,
                                            const void* paramsPtr,
                                            float* const* channels,
                                            const WasmAudioProcessContext& context) const
      {
         (void)runtimeState;
         if (!paramsPtr || !channels)
            return;

         // `channels` arrive zeroed (sumsAudioInputs() == false); a port may carry several cables.
         // Equal-power law normalised to unity at centre: mono inputs get +3 dB at the extremes,
         // stereo inputs are balanced (each side capped at unity, the far side fades out).
         const auto& params = *static_cast<const MixerParams*>(paramsPtr);
         for (int c = 0; c < context.audioInputCount; ++c)
         {
            const WasmAudioInput& input = context.audioInputs[c];
            if (!input.channels[0] || input.port < 0 || input.port >= kMixerChannelCount)
               continue;
            const float gain = params.level[input.port] * params.master;
            if (gain <= 0.0f)
               continue;
            const float theta = std::max(0.0f, std::min(1.0f, params.pan[input.port])) * kHalfPi;
            float gainL = kSqrt2 * std::cos(theta);
            float gainR = kSqrt2 * std::sin(theta);
            if (context.numChannels < 2)
            {
               // One-channel block (processMono): pan does not apply.
               for (int ch = 0; ch < input.numChannels; ++ch)
                  dsp::accumulateScaled(channels[0], input.channels[ch], context.numSamples, gain / input.numChannels);
            }
            else if (input.numChannels == 2)
            {
               dsp::accumulateScaled(channels[0], input.channels[0], context.numSamples, gain * std::min(1.0f, gainL));
               dsp::accumulateScaled(channels[1], input.channels[1], context.numSamples, gain * std::min(1.0f, gainR));
            }
            else
            {
               dsp::accumulatePanned(channels[0], channels[1], input.channels[0], context.numSamples,
                                     gain * gainL, gain * gainR);
            }
         }
      }

      BESPOKE_REGISTER_MODULE(MixerModuleAdapter);

   } // namespace wasm
} // namespace bespoke
