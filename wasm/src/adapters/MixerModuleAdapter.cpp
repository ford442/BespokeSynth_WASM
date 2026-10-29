/**
 * BespokeSynth WASM - Four-channel summing mixer adapter implementation
 */

#include "BespokeWasm/adapters/MixerModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
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
         constexpr float kHalfPi = 1.57079632679489661923f;
         constexpr float kSqrt2 = 1.41421356237309504880f;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "level1", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(MixerParams, level[0]), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "level2", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(MixerParams, level[1]), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 2, .name = "level3", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(MixerParams, level[2]), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 3, .name = "level4", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(MixerParams, level[3]), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 4, .name = "pan1", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(MixerParams, pan[0]), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 5, .name = "pan2", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(MixerParams, pan[1]), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 6, .name = "pan3", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(MixerParams, pan[2]), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 7, .name = "pan4", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(MixerParams, pan[3]), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 8, .name = "master", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(MixerParams, master), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = {
            { PortType::Audio, "In 1" },
            { PortType::Audio, "In 2" },
            { PortType::Audio, "In 3" },
            { PortType::Audio, "In 4" },
         };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& mixerSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "level1", "Ch 1", SliderFormat::Percent },
                                                                           { "level2", "Ch 2", SliderFormat::Percent },
                                                                           { "level3", "Ch 3", SliderFormat::Percent },
                                                                           { "level4", "Ch 4", SliderFormat::Percent },
                                                                           { "pan1", "Pan 1", SliderFormat::Pan },
                                                                           { "pan2", "Pan 2", SliderFormat::Pan },
                                                                           { "pan3", "Pan 3", SliderFormat::Pan },
                                                                           { "pan4", "Pan 4", SliderFormat::Pan },
                                                                           { "master", "Master", SliderFormat::Percent },
                                                                           });
            return specs;
         }
      }

      ParamTable MixerModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> MixerModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> MixerModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> MixerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), mixerSpecs(), 150.0f);
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
                  dsp::accumulateScaled(channels[0], input.channels[ch], context.numSamples, gain / static_cast<float>(input.numChannels));
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
