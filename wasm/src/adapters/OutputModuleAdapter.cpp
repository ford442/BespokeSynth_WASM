/**
 * BespokeSynth WASM - Output sink adapter implementation
 */

#include "BespokeWasm/adapters/OutputModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      std::vector<WasmControlDescriptor> OutputModuleAdapter::controlDescriptors() const
      {
         return { { "level", 0.0f }, { "limit", 1.0f } };
      }

      std::vector<PortDescriptor> OutputModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" } };
      }

      std::vector<PortDescriptor> OutputModuleAdapter::outputPorts() const
      {
         return {};
      }

      std::unique_ptr<Module> OutputModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<OutputModule>(id);
      }

      void OutputModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) OutputParams();
         params->limit = wasmControlValue(controls, "limit", 1.0f) >= 0.5f;
      }

      void OutputModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) OutputRuntimeState();
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
