/**
 * BespokeSynth WASM - Distortion adapter implementation
 */

#include "BespokeWasm/adapters/DistortionModuleAdapter.h"
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
         constexpr float kPeakDecaySeconds = 0.1f;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "type", .minValue = 0.0f, .maxValue = 6.0f, .defaultValue = 0.0f, .kind = ParamKind::IntRound, .offset = offsetof(DistortionParams, type), .flags = kParamClamp },
            { .id = 1, .name = "clip", .minValue = 0.001f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(DistortionParams, clip), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 2, .name = "preamp", .minValue = 1.0f, .maxValue = 10.0f, .defaultValue = 1.0f, .offset = offsetof(DistortionParams, preamp), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 3, .name = "fuzz", .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .offset = offsetof(DistortionParams, fuzz), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 4, .name = "centerInput", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Bool, .offset = offsetof(DistortionParams, centerInput) },
            { .id = 5, .name = "level", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(DistortionParams, level), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& distortionSpecs()
         {
            static const std::vector<SliderSpec> specs =
            slidersFromParams(kParams,
                              {
                              { "type", "Type", SliderFormat::Choice, { "clean", "warm", "dirty", "soft", "asym", "fold", "grungy" } },
                              { "clip", "Clip", SliderFormat::Decimal },
                              { "preamp", "Preamp", SliderFormat::Decimal },
                              { "fuzz", "Fuzz", SliderFormat::Decimal },
                              { "centerInput", "Center input", SliderFormat::Choice, { "off", "on" } },
                              { "level", "Level", SliderFormat::Percent },
                              });
            return specs;
         }
      }

      DistortionRuntimeState::DistortionRuntimeState()
      {
         for (auto& filter : dcRemover)
            filter.SetFilterType(kFilterType_Highpass);
      }

      float DistortionModuleAdapter::shape(DistortionType type, float x)
      {
         switch (type)
         {
            case DistortionType::Clean:
               return std::tanh(x);
            case DistortionType::Warm:
               return std::sin(x);
            case DistortionType::Dirty:
               return std::max(-1.0f, std::min(1.0f, x));
            case DistortionType::Soft:
               if (x > 1.0f)
                  return 0.66666f;
               if (x < -1.0f)
                  return -0.66666f;
               return x - (x * x * x) / 3.0f;
            case DistortionType::Asymmetric:
               if (x >= 0.320018f)
                  return 0.630035f;
               if (x >= -0.08905f)
                  return -6.153f * x * x + 3.9375f * x;
               if (x >= -1.0f)
               {
                  const float a = std::fabs(x) - 0.032847f;
                  return -0.75f * (1.0f - std::pow(1.0f - a, 12.0f) + 0.333f * a) + 0.01f;
               }
               return -0.9818f;
            case DistortionType::Fold:
            {
               float folded = std::max(-100.0f, std::min(100.0f, x));
               while (folded > 1.0f || folded < -1.0f)
               {
                  if (folded > 1.0f)
                     folded = 2.0f - folded;
                  if (folded < -1.0f)
                     folded = -2.0f - folded;
               }
               return folded;
            }
            case DistortionType::Grungy:
               return std::asin(std::max(-1.0f, std::min(1.0f, x)));
         }
         return x;
      }

      ParamTable DistortionModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> DistortionModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> DistortionModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> DistortionModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), distortionSpecs(), 170.0f);
      }

      void DistortionModuleAdapter::processAudio(void* runtimeState,
                                                 const void* paramsPtr,
                                                 float* const* channels,
                                                 const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;
         auto& state = stateOf(runtimeState);
         const auto& params = paramsOf(paramsPtr);
         const float sampleRate = std::max(1.0f, context.sampleRate);
         if (state.lastSampleRate != sampleRate)
         {
            for (auto& filter : state.dcRemover)
            {
               filter.SetSampleRate(sampleRate);
               filter.SetFilterParams(10.0, std::sqrt(2.0) / 2.0);
               filter.UpdateFilterCoeff();
            }
            state.lastSampleRate = sampleRate;
         }

         const DistortionType type = static_cast<DistortionType>(std::max(0, std::min(6, params.type)));
         const float clip = std::max(0.001f, params.clip);
         const float gain = 1.0f / (clip * clip * clip);
         // Asym and fold halve the input before the preamp (desktop).
         const float inputScale = type == DistortionType::Asymmetric || type == DistortionType::Fold ? 0.5f : 1.0f;
         const float decay = std::pow(0.5f, 1.0f / (kPeakDecaySeconds * sampleRate));
         for (int ch = 0; ch < context.numChannels && ch < 2; ++ch)
         {
            float* buffer = channels[ch];
            if (params.centerInput)
               state.dcRemover[ch].Filter(buffer, context.numSamples);
            float peak = state.peak[ch];
            for (int i = 0; i < context.numSamples; ++i)
            {
               const float magnitude = std::fabs(buffer[i]);
               peak = magnitude >= peak ? magnitude : peak * decay;
               const float driven = (buffer[i] * inputScale + params.fuzz * peak) * params.preamp * gain;
               buffer[i] = shape(type, driven) / gain * params.level;
            }
            state.peak[ch] = peak;
         }
      }

      BESPOKE_REGISTER_MODULE(DistortionModuleAdapter);

   } // namespace wasm
} // namespace bespoke
