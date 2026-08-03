/**
 * BespokeSynth WASM - Delay effect adapter implementation
 */

#include "BespokeWasm/adapters/DelayModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <cmath>

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

      std::unique_ptr<Module> DelayModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<DelayModule>(id);
      }

      void DelayModuleAdapter::fillAudioGraphNode(const std::map<std::string, float>& controls,
                                                  AudioGraphNode& node) const
      {
         auto get = [&](const char* name, float fallback) -> float
         {
            auto it = controls.find(name);
            return it != controls.end() ? it->second : fallback;
         };
         node.delayTime = get("time", 0.25f);
         node.delayFeedback = get("feedback", 0.35f);
         node.delayMix = get("mix", 0.35f);
      }

      void DelayModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         auto* state = static_cast<DelayAdapterRuntimeState*>(runtimeState);
         state->buffer.clear();
         state->writeIndex = 0;
         state->lastSampleRate = 0.0f;
      }

      void DelayModuleAdapter::processAudio(void* runtimeState,
                                            const AudioGraphNode& node,
                                            float* buffer,
                                            const WasmAudioProcessContext& context) const
      {
         auto& state = *static_cast<DelayAdapterRuntimeState*>(runtimeState);
         const size_t capacity = static_cast<size_t>(
            std::max(1.0f, context.sampleRate * kMaxDelaySeconds));

         if (state.lastSampleRate != context.sampleRate || state.buffer.size() != capacity)
         {
            state.buffer.assign(capacity, 0.0f);
            state.writeIndex = 0;
            state.lastSampleRate = context.sampleRate;
         }

         const float delaySeconds = clampFloat(node.delayTime, 0.001f, kMaxDelaySeconds);
         const float feedback = clampFloat(node.delayFeedback, 0.0f, 0.95f);
         const float mix = clampFloat(node.delayMix, 0.0f, 1.0f);
         const float delaySamples = delaySeconds * context.sampleRate;

         for (int i = 0; i < context.numSamples; ++i)
         {
            const float input = buffer[i];
            float readPos = static_cast<float>(state.writeIndex) - delaySamples;
            while (readPos < 0.0f)
               readPos += static_cast<float>(state.buffer.size());

            const size_t i0 = static_cast<size_t>(readPos) % state.buffer.size();
            const size_t i1 = (i0 + 1) % state.buffer.size();
            const float frac = readPos - std::floor(readPos);
            const float delayed = state.buffer[i0] * (1.0f - frac) + state.buffer[i1] * frac;

            state.buffer[state.writeIndex] = input + delayed * feedback;
            state.writeIndex = (state.writeIndex + 1) % state.buffer.size();
            buffer[i] = input * (1.0f - mix) + delayed * mix;
         }
      }

   } // namespace wasm
} // namespace bespoke
