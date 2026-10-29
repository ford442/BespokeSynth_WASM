/**
 * BespokeSynth WASM - Stereo width adapter implementation
 */

#include "BespokeWasm/adapters/StereoWidthModuleAdapter.h"
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
            { .id = 0, .name = "width", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 1.0f, .offset = offsetof(StereoWidthParams, width), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "widen", .minValue = 0.0f, .maxValue = StereoWidthModuleAdapter::kMaxHaasMs, .defaultValue = 0.0f, .offset = offsetof(StereoWidthParams, haasMs), .flags = kParamClamp },
            { .id = 2, .name = "level", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(StereoWidthParams, level), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In", PortChannels::Stereo } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& widthSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "width", "Width", SliderFormat::Percent },
                                                                           { "widen", "Haas", SliderFormat::Millis },
                                                                           { "level", "Level", SliderFormat::Percent },
                                                                           });
            return specs;
         }
      }

      int StereoWidthModuleAdapter::outputChannelCount(const void* params, const WasmAudioProcessContext& context) const
      {
         (void)params;
         (void)context;
         return 2;
      }

      ParamTable StereoWidthModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> StereoWidthModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> StereoWidthModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> StereoWidthModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), widthSpecs(), 150.0f);
      }

      void StereoWidthModuleAdapter::processAudio(void* runtimeState,
                                                  const void* paramsPtr,
                                                  float* const* channels,
                                                  const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels || context.numChannels < 2)
            return;
         auto& state = stateOf(runtimeState);
         const auto& params = paramsOf(paramsPtr);
         constexpr int kCapacity = StereoWidthRuntimeState::kHaasCapacity;
         const int haas = std::max(0, std::min(kCapacity - 1, static_cast<int>(std::lround(params.haasMs * 0.001f * context.sampleRate))));
         float* left = channels[0];
         float* right = channels[1];
         for (int i = 0; i < context.numSamples; ++i)
         {
            const float mid = 0.5f * (left[i] + right[i]);
            const float side = 0.5f * (left[i] - right[i]) * params.width;
            const float outR = mid - side;
            state.haas[state.writeIndex] = outR;
            const int read = (state.writeIndex - haas + kCapacity) % kCapacity;
            state.writeIndex = (state.writeIndex + 1) % kCapacity;
            left[i] = (mid + side) * params.level;
            right[i] = state.haas[read] * params.level;
         }
      }

      BESPOKE_REGISTER_MODULE(StereoWidthModuleAdapter);

   } // namespace wasm
} // namespace bespoke
