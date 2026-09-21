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
                                                 float* buffer,
                                                 const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<CompressorRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const CompressorParams*>(paramsPtr);
         const float sampleRate = std::max(1.0f, context.sampleRate);
         const float attack = coefficientForMs(params.attackMs, sampleRate);
         const float release = coefficientForMs(params.releaseMs, sampleRate);
         // Below this linear level the gain computer is exactly 0 dB: skip the log.
         const float quietLinear = std::pow(10.0f, (params.thresholdDb - params.kneeDb * 0.5f) / 20.0f);

         // `buffer` arrives zeroed; the main input is port 0, the detector key is port 1 when cabled.
         accumulateAudioInputPort(context, kPortMain, buffer);
         bool hasSidechain = false;
         for (int c = 0; c < context.audioInputCount; ++c)
            hasSidechain |= context.audioInputs[c].port == kPortSidechain && context.audioInputs[c].buffer;

         float key[kChunk];
         for (int start = 0; start < context.numSamples; start += kChunk)
         {
            const int count = std::min(kChunk, context.numSamples - start);
            if (hasSidechain)
            {
               std::fill(key, key + count, 0.0f);
               for (int c = 0; c < context.audioInputCount; ++c)
               {
                  const WasmAudioInput& input = context.audioInputs[c];
                  if (input.port != kPortSidechain || !input.buffer)
                     continue;
                  for (int i = 0; i < count; ++i)
                     key[i] += input.buffer[start + i];
               }
            }
            else
            {
               std::copy(buffer + start, buffer + start + count, key);
            }

            for (int i = 0; i < count; ++i)
            {
               const float level = std::fabs(key[i]);
               const float targetDb =
               level > quietLinear ? gainComputerDb(20.0f * std::log10(level), params) : 0.0f;
               const float coeff = targetDb < state.envelopeDb ? attack : release;
               state.envelopeDb = targetDb + coeff * (state.envelopeDb - targetDb);
               buffer[start + i] *= std::pow(10.0f, (state.envelopeDb + params.makeupDb) / 20.0f);
            }
         }
      }

      BESPOKE_REGISTER_MODULE(CompressorModuleAdapter);

   } // namespace wasm
} // namespace bespoke
