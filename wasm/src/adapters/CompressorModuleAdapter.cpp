/**
 * BespokeSynth WASM - Feed-forward compressor adapter implementation
 */

#include "BespokeWasm/adapters/CompressorModuleAdapter.h"
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
         constexpr int kChunk = 64;
         constexpr int kPortMain = 0;
         constexpr int kPortSidechain = 1;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "threshold", .minValue = -60.0f, .maxValue = 0.0f, .defaultValue = -18.0f, .offset = offsetof(CompressorParams, thresholdDb), .flags = kParamClamp },
            { .id = 1, .name = "ratio", .minValue = 1.0f, .maxValue = 20.0f, .defaultValue = 4.0f, .offset = offsetof(CompressorParams, ratio), .flags = kParamClamp },
            { .id = 2, .name = "attack", .minValue = 0.1f, .maxValue = 100.0f, .defaultValue = 5.0f, .offset = offsetof(CompressorParams, attackMs), .flags = kParamClamp },
            { .id = 3, .name = "release", .minValue = 10.0f, .maxValue = 1000.0f, .defaultValue = 120.0f, .offset = offsetof(CompressorParams, releaseMs), .flags = kParamClamp },
            { .id = 4, .name = "makeup", .minValue = 0.0f, .maxValue = 24.0f, .defaultValue = 0.0f, .offset = offsetof(CompressorParams, makeupDb), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 5, .name = "knee", .minValue = 0.0f, .maxValue = 24.0f, .defaultValue = 6.0f, .offset = offsetof(CompressorParams, kneeDb), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" }, { PortType::Audio, "Sidechain" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& compressorSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "threshold", "Thresh", SliderFormat::Decibels },
                                                                           { "ratio", "Ratio", SliderFormat::Ratio },
                                                                           { "attack", "Attack", SliderFormat::Millis },
                                                                           { "release", "Release", SliderFormat::Millis },
                                                                           { "makeup", "Makeup", SliderFormat::Decibels },
                                                                           { "knee", "Knee", SliderFormat::Decibels },
                                                                           });
            return specs;
         }

         float coefficientForMs(float ms, float sampleRate)
         {
            return std::exp(-1.0f / (std::max(0.01f, ms) * 0.001f * sampleRate));
         }
      }

      float CompressorModuleAdapter::gainComputerDb(float levelDb, const CompressorParams& params)
      {
         const float slope = 1.0f / std::max(1.0f, params.ratio) - 1.0f;
         const float over = levelDb - params.thresholdDb;
         const float knee = std::max(0.0f, params.kneeDb);
         if (2.0f * over <= -knee)
            return 0.0f;
         if (knee > 0.0f && 2.0f * std::fabs(over) < knee)
         {
            const float x = over + knee * 0.5f;
            return slope * x * x / (2.0f * knee);
         }
         return slope * over;
      }

      ParamTable CompressorModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> CompressorModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> CompressorModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> CompressorModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), compressorSpecs());
      }


      int CompressorModuleAdapter::outputChannelCount(const void* params, const WasmAudioProcessContext& context) const
      {
         (void)params;
         return std::max(1, audioInputPortChannels(context, kPortMain));
      }


      void CompressorModuleAdapter::processAudio(void* runtimeState,
                                                 const void* paramsPtr,
                                                 float* const* channels,
                                                 const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;

         auto& state = *static_cast<CompressorRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const CompressorParams*>(paramsPtr);
         const float sampleRate = std::max(1.0f, context.sampleRate);
         const float attack = coefficientForMs(params.attackMs, sampleRate);
         const float release = coefficientForMs(params.releaseMs, sampleRate);
         // Below this linear level the gain computer is exactly 0 dB: skip the log.
         const float quietLinear = std::pow(10.0f, (params.thresholdDb - params.kneeDb * 0.5f) / 20.0f);

         // `channels` arrive zeroed; the main input is port 0, the detector key is port 1 when cabled.
         accumulateAudioInputPort(context, kPortMain, channels, context.numChannels);
         const bool hasSidechain = audioInputPortChannels(context, kPortSidechain) > 0;

         // Linked stereo: one detector on max(|L|, |R|) of the key, one gain for every channel.
         float key[kChunk];
         for (int start = 0; start < context.numSamples; start += kChunk)
         {
            const int count = std::min(kChunk, context.numSamples - start);
            std::fill(key, key + count, 0.0f);
            if (hasSidechain)
            {
               for (int c = 0; c < context.audioInputCount; ++c)
               {
                  const WasmAudioInput& input = context.audioInputs[c];
                  if (input.port != kPortSidechain || !input.channels[0])
                     continue;
                  for (int i = 0; i < count; ++i)
                  {
                     const float l = input.channels[0][start + i];
                     const float r = input.channels[1][start + i];
                     key[i] = std::max(key[i], std::max(std::fabs(l), std::fabs(r)));
                  }
               }
            }
            else
            {
               for (int ch = 0; ch < context.numChannels; ++ch)
               {
                  for (int i = 0; i < count; ++i)
                     key[i] = std::max(key[i], std::fabs(channels[ch][start + i]));
               }
            }

            for (int i = 0; i < count; ++i)
            {
               const float level = key[i];
               const float targetDb =
               level > quietLinear ? gainComputerDb(20.0f * std::log10(level), params) : 0.0f;
               const float coeff = targetDb < state.envelopeDb ? attack : release;
               state.envelopeDb = targetDb + coeff * (state.envelopeDb - targetDb);
               const float gain = std::pow(10.0f, (state.envelopeDb + params.makeupDb) / 20.0f);
               for (int ch = 0; ch < context.numChannels; ++ch)
                  channels[ch][start + i] *= gain;
            }
         }
      }

      BESPOKE_REGISTER_MODULE(CompressorModuleAdapter);

   } // namespace wasm
} // namespace bespoke
