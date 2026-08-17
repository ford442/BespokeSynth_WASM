/**
 * BespokeSynth WASM - LFO modulation adapter implementation
 */

#include "BespokeWasm/adapters/LfoModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <cmath>
#include <new>

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
      }

      std::vector<WasmControlDescriptor> LfoModuleAdapter::controlDescriptors() const
      {
         return {
            { "rate", 1.0f },
            { "depth", 1.0f },
            { "shape", 0.0f },
         };
      }

      std::vector<PortDescriptor> LfoModuleAdapter::inputPorts() const
      {
         return {};
      }

      std::vector<PortDescriptor> LfoModuleAdapter::outputPorts() const
      {
         return { { PortType::Modulation, "Mod" } };
      }

      std::unique_ptr<Module> LfoModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<LFOModule>(id);
      }

      void LfoModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) LfoParams();
         params->rate = wasmControlValue(controls, "rate", 1.0f);
         params->depth = wasmControlValue(controls, "depth", 1.0f);
         params->shape = static_cast<int>(wasmControlValue(controls, "shape", 0.0f));
      }

      void LfoModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) LfoAdapterRuntimeState();
      }

      void LfoModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<LfoAdapterRuntimeState*>(runtimeState)->~LfoAdapterRuntimeState();
      }

      void LfoModuleAdapter::processAudio(void* runtimeState,
                                          const void* paramsPtr,
                                          float* buffer,
                                          const WasmAudioProcessContext& context) const
      {
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
