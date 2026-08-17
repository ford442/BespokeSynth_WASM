/**
 * BespokeSynth WASM - Delay effect adapter implementation
 */

#include "BespokeWasm/adapters/DelayModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }
      }

      std::vector<WasmControlDescriptor> DelayModuleAdapter::controlDescriptors() const
      {
         return {
            { "time", 0.25f },
            { "feedback", 0.35f },
            { "mix", 0.35f },
         };
      }

      std::vector<PortDescriptor> DelayModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" } };
      }

      std::vector<PortDescriptor> DelayModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> DelayModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<DelayModule>(id);
      }

      void DelayModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) DelayParams();
         params->time = wasmControlValue(controls, "time", 0.25f);
         params->feedback = wasmControlValue(controls, "feedback", 0.35f);
         params->mix = wasmControlValue(controls, "mix", 0.35f);
      }

      size_t DelayModuleAdapter::runtimeStateSize() const
      {
         return sizeof(DelayAdapterRuntimeState) + sizeof(float) * kMaxDelaySamples;
      }

      void DelayModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         auto* state = new (runtimeState) DelayAdapterRuntimeState();
         state->buffer = reinterpret_cast<float*>(
            reinterpret_cast<uint8_t*>(runtimeState) + sizeof(DelayAdapterRuntimeState));
         state->usedCapacity = kMaxDelaySamples;
         state->writeIndex = 0;
         state->lastSampleRate = 0.0f;
         std::memset(state->buffer, 0, sizeof(float) * kMaxDelaySamples);
      }

      void DelayModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<DelayAdapterRuntimeState*>(runtimeState)->~DelayAdapterRuntimeState();
      }

      void DelayModuleAdapter::processAudio(void* runtimeState,
                                            const void* paramsPtr,
                                            float* buffer,
                                            const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<DelayAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const DelayParams*>(paramsPtr);
         if (!state.buffer || state.usedCapacity == 0)
            return;

         const size_t capacity = std::min(
            state.usedCapacity,
            static_cast<size_t>(std::max(1.0f, context.sampleRate * kMaxDelaySeconds)));

         if (state.lastSampleRate != context.sampleRate)
         {
            std::memset(state.buffer, 0, sizeof(float) * state.usedCapacity);
            state.writeIndex = 0;
            state.lastSampleRate = context.sampleRate;
         }

         const float delaySeconds = clampFloat(params.time, 0.001f, kMaxDelaySeconds);
         const float feedback = clampFloat(params.feedback, 0.0f, 0.95f);
         const float mix = clampFloat(params.mix, 0.0f, 1.0f);
         const float delaySamples = delaySeconds * context.sampleRate;

         for (int i = 0; i < context.numSamples; ++i)
         {
            const float input = buffer[i];
            float readPos = static_cast<float>(state.writeIndex) - delaySamples;
            while (readPos < 0.0f)
               readPos += static_cast<float>(capacity);

            const size_t i0 = static_cast<size_t>(readPos) % capacity;
            const size_t i1 = (i0 + 1) % capacity;
            const float frac = readPos - std::floor(readPos);
            const float delayed = state.buffer[i0] * (1.0f - frac) + state.buffer[i1] * frac;

            state.buffer[state.writeIndex] = input + delayed * feedback;
            state.writeIndex = (state.writeIndex + 1) % capacity;
            buffer[i] = input * (1.0f - mix) + delayed * mix;
         }
      }

      BESPOKE_REGISTER_MODULE(DelayModuleAdapter);

   } // namespace wasm
} // namespace bespoke
