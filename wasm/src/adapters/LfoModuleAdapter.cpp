/**
 * BespokeSynth WASM - LFO modulation adapter implementation
 */

#include "BespokeWasm/adapters/LfoModuleAdapter.h"
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
         constexpr float kTwoPi = 6.28318530717958647692f;

         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }

         void updateOscillatorType(LfoAdapterRuntimeState& state, int shape)
         {
            switch (shape % 4)
            {
               case 1:
                  state.oscillatorType = kOsc_Saw;
                  break;
               case 2:
                  state.oscillatorType = kOsc_Square;
                  break;
               case 3:
                  state.oscillatorType = kOsc_Tri;
                  break;
               default:
                  state.oscillatorType = kOsc_Sin;
                  break;
            }
            state.oscillator.SetType(state.oscillatorType);
         }

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "rate", .minValue = 0.05f, .maxValue = 20.0f, .defaultValue = 1.0f, .offset = offsetof(LfoParams, rate) },
            { .id = 1, .name = "depth", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(LfoParams, depth), .smoothingMs = kParamSmoothingMs },
            { .id = 2, .name = "shape", .minValue = 0.0f, .maxValue = 3.0f, .defaultValue = 0.0f, .kind = ParamKind::Int, .offset = offsetof(LfoParams, shape) },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kOutputs[] = { { PortType::Modulation, "Mod" } };
      }

      ParamTable LfoModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> LfoModuleAdapter::inputPorts() const
      {
         return {};
      }

      std::span<const PortDescriptor> LfoModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> LfoModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<LFOModule>(id);
      }

      void LfoModuleAdapter::processAudio(void* runtimeState,
                                          const void* paramsPtr,
                                          float* const* channels,
                                          const WasmAudioProcessContext& context) const
      {
         float* buffer = channels ? channels[0] : nullptr;
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<LfoAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const LfoParams*>(paramsPtr);
         updateOscillatorType(state, params.shape);
         const float depth = clampFloat(params.depth, 0.0f, 1.0f);
         const float rate = params.rate;
         const float sampleRate = std::max(1.0f, context.sampleRate);

         for (int i = 0; i < context.numSamples; ++i)
         {
            const float bipolar = state.oscillator.Value(state.phase);
            state.phase += kTwoPi * clampFloat(rate, 0.0f, sampleRate * 0.45f) / sampleRate;
            if (state.phase >= kTwoPi)
               state.phase = std::fmod(state.phase, kTwoPi);
            buffer[i] = bipolar * depth;
         }
      }

      BESPOKE_REGISTER_MODULE(LfoModuleAdapter);

   } // namespace wasm
} // namespace bespoke
