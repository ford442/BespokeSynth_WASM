/**
 * BespokeSynth WASM - Mono-to-stereo panner adapter implementation
 */

#include "BespokeWasm/adapters/PannerModuleAdapter.h"
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
         constexpr int kPortIn = 0;
         constexpr int kChunk = 64;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "pan", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(PannerParams, pan), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "gain", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(PannerParams, gain), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In", PortChannels::Mono },
                                                { PortType::Modulation, "Pan Mod" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& pannerSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "pan", "Pan", SliderFormat::Pan },
                                                                           { "gain", "Gain", SliderFormat::Percent },
                                                                           });
            return specs;
         }
      }

      int PannerModuleAdapter::outputChannelCount(const void* params, const WasmAudioProcessContext& context) const
      {
         (void)params;
         (void)context;
         return 2;
      }

      ParamTable PannerModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> PannerModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> PannerModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> PannerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), pannerSpecs(), 150.0f);
      }

      void PannerModuleAdapter::processAudio(void* runtimeState,
                                             const void* paramsPtr,
                                             float* const* channels,
                                             const WasmAudioProcessContext& context) const
      {
         (void)runtimeState;
         if (!paramsPtr || !channels || context.numChannels < 2)
            return;
         const auto& params = *static_cast<const PannerParams*>(paramsPtr);

         // `channels` arrive zeroed; the plan already folded stereo cables to mono for this port.
         float mono[kChunk];
         for (int start = 0; start < context.numSamples; start += kChunk)
         {
            const int count = std::min(kChunk, context.numSamples - start);
            std::fill(mono, mono + count, 0.0f);
            for (int c = 0; c < context.audioInputCount; ++c)
            {
               const WasmAudioInput& input = context.audioInputs[c];
               if (input.port == kPortIn && input.channels[0])
                  dsp::accumulateScaled(mono, input.channels[0] + start, count, 1.0f);
            }
            if (!context.modulationBuffer)
            {
               const float theta = params.pan * kHalfPi;
               dsp::accumulatePanned(channels[0] + start, channels[1] + start, mono, count,
                                     params.gain * kSqrt2 * std::cos(theta), params.gain * kSqrt2 * std::sin(theta));
               continue;
            }
            // Pan Mod: an LFO at depth 1 sweeps the full width around the knob.
            for (int i = 0; i < count; ++i)
            {
               const float pan = std::max(0.0f, std::min(1.0f, params.pan + 0.5f * context.modulationBuffer[start + i]));
               const float theta = pan * kHalfPi;
               channels[0][start + i] = mono[i] * params.gain * kSqrt2 * std::cos(theta);
               channels[1][start + i] = mono[i] * params.gain * kSqrt2 * std::sin(theta);
            }
         }
      }

      BESPOKE_REGISTER_MODULE(PannerModuleAdapter);

   } // namespace wasm
} // namespace bespoke
