/**
 * BespokeSynth WASM - Feed-forward compressor adapter implementation
 */

#include "BespokeWasm/adapters/CompressorModuleAdapter.h"
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
         constexpr int kChunk = 64;
         constexpr int kPortMain = 0;
         constexpr int kPortSidechain = 1;

         const std::vector<SliderSpec>& compressorSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "threshold", "Thresh", -60.0f, 0.0f, -18.0f, SliderFormat::Decibels },
               { "ratio", "Ratio", 1.0f, 20.0f, 4.0f, SliderFormat::Ratio },
               { "attack", "Attack", 0.1f, 100.0f, 5.0f, SliderFormat::Millis },
               { "release", "Release", 10.0f, 1000.0f, 120.0f, SliderFormat::Millis },
               { "makeup", "Makeup", 0.0f, 24.0f, 0.0f, SliderFormat::Decibels },
               { "knee", "Knee", 0.0f, 24.0f, 6.0f, SliderFormat::Decibels },
            };
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

      std::vector<WasmControlDescriptor> CompressorModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(compressorSpecs());
      }

      std::vector<PortDescriptor> CompressorModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" }, { PortType::Audio, "Sidechain" } };
      }

      std::vector<PortDescriptor> CompressorModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> CompressorModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), compressorSpecs());
      }

      void CompressorModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         const auto& specs = compressorSpecs();
         auto* params = new (dst) CompressorParams();
         params->thresholdDb = sliderSpecValue(specs, controls, "threshold");
         params->ratio = sliderSpecValue(specs, controls, "ratio");
         params->attackMs = sliderSpecValue(specs, controls, "attack");
         params->releaseMs = sliderSpecValue(specs, controls, "release");
         params->makeupDb = sliderSpecValue(specs, controls, "makeup");
         params->kneeDb = sliderSpecValue(specs, controls, "knee");
      }

      int CompressorModuleAdapter::outputChannelCount(const void* params, const WasmAudioProcessContext& context) const
      {
         (void)params;
         return std::max(1, audioInputPortChannels(context, kPortMain));
      }

      void CompressorModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) CompressorRuntimeState();
      }

      void CompressorModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<CompressorRuntimeState*>(runtimeState)->~CompressorRuntimeState();
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
