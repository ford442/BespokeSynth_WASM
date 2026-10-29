/**
 * BespokeSynth WASM - Output sink adapter implementation
 */

#include "BespokeWasm/adapters/OutputModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
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
            // UI-only: the Output panel's level display; the graph never reads it.
            { .id = 0, .name = "level", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f },
            { .id = 1, .name = "limit", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .kind = ParamKind::Bool, .offset = offsetof(OutputParams, limit) },
            { .id = 2, .name = "dcblock", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .kind = ParamKind::Bool, .offset = offsetof(OutputParams, dcBlock) },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" } };
      }

      ParamTable OutputModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> OutputModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> OutputModuleAdapter::outputPorts() const
      {
         return {};
      }

      std::unique_ptr<Module> OutputModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<OutputModule>(id);
      }

      void OutputModuleAdapter::processAudio(void* runtimeState,
                                             const void* paramsPtr,
                                             float* const* channels,
                                             const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;

         auto& state = *static_cast<OutputRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const OutputParams*>(paramsPtr);
         if (params.dcBlock)
         {
            const float r = std::exp(-6.2831853f * kDcBlockHz / std::max(1.0f, context.sampleRate));
            for (int ch = 0; ch < context.numChannels; ++ch)
            {
               float x1 = state.dcInput[ch];
               float y1 = state.dcOutput[ch];
               float* samples = channels[ch];
               for (int i = 0; i < context.numSamples; ++i)
               {
                  const float x = samples[i];
                  const float y = x - x1 + r * y1;
                  x1 = x;
                  // Non-finite input is zeroed by the limiter below; flush the tail before it goes denormal.
                  y1 = std::isfinite(y) && std::fabs(y) > 1e-20f ? y : 0.0f;
                  samples[i] = y;
               }
               state.dcInput[ch] = std::isfinite(x1) ? x1 : 0.0f;
               state.dcOutput[ch] = y1;
            }
            // A mono block leaves R's filter where it was; restart it from L when R comes back.
            if (context.numChannels == 1)
            {
               state.dcInput[1] = state.dcInput[0];
               state.dcOutput[1] = state.dcOutput[0];
            }
         }
         if (!params.limit)
         {
            state.gain = 1.0f;
            return; // the engine's final clamp is the only protection
         }

         const float release = std::exp(-1.0f / (kLimiterReleaseMs * 0.001f * std::max(1.0f, context.sampleRate)));
         for (int i = 0; i < context.numSamples; ++i)
         {
            float peak = 0.0f;
            bool finite = true;
            for (int ch = 0; ch < context.numChannels; ++ch)
            {
               finite = finite && std::isfinite(channels[ch][i]);
               peak = std::max(peak, std::fabs(channels[ch][i]));
            }
            if (!finite)
            {
               // A NaN/inf from upstream must not reach the device or poison the envelope.
               for (int ch = 0; ch < context.numChannels; ++ch)
                  channels[ch][i] = 0.0f;
               continue;
            }
            const float target = peak > kLimiterCeiling ? kLimiterCeiling / peak : 1.0f;
            state.gain = target < state.gain ? target : target + (state.gain - target) * release;
            for (int ch = 0; ch < context.numChannels; ++ch)
               channels[ch][i] *= state.gain;
         }
      }

      BESPOKE_REGISTER_MODULE(OutputModuleAdapter);

   } // namespace wasm
} // namespace bespoke
