/**
 * BespokeSynth WASM - Noise source adapter implementation
 */

#include "BespokeWasm/adapters/NoiseModuleAdapter.h"
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
         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }

         float nextWhite(NoiseAdapterRuntimeState& state)
         {
            state.rng = state.rng * 1664525u + 1013904223u;
            const float unit = static_cast<float>(state.rng >> 8) * (1.0f / 16777216.0f);
            return unit * 2.0f - 1.0f;
         }
      }

      std::vector<WasmControlDescriptor> NoiseModuleAdapter::controlDescriptors() const
      {
         return {
            { "volume", 0.35f },
            { "color", 0.0f },
         };
      }

      std::unique_ptr<Module> NoiseModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<NoiseModule>(id);
      }

      void NoiseModuleAdapter::fillAudioGraphNode(const std::map<std::string, float>& controls,
                                                  AudioGraphNode& node) const
      {
         auto get = [&](const char* name, float fallback) -> float
         {
            auto it = controls.find(name);
            return it != controls.end() ? it->second : fallback;
         };
         node.noiseVolume = get("volume", 0.35f);
         node.noiseColor = static_cast<int>(get("color", 0.0f));
      }

      void NoiseModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) NoiseAdapterRuntimeState();
      }

      void NoiseModuleAdapter::processAudio(void* runtimeState,
                                            const AudioGraphNode& node,
                                            float* buffer,
                                            const WasmAudioProcessContext& context) const
      {
         auto& state = *static_cast<NoiseAdapterRuntimeState*>(runtimeState);
         const float volume = clampFloat(node.noiseVolume, 0.0f, 1.0f);
         const bool pink = (node.noiseColor % 2) == 1;

         for (int i = 0; i < context.numSamples; ++i)
         {
            float sample = nextWhite(state);
            if (pink)
            {
               // Paul Kellet approximate pink filter
               state.pinkB0 = 0.99765f * state.pinkB0 + sample * 0.0990460f;
               state.pinkB1 = 0.96300f * state.pinkB1 + sample * 0.2965164f;
               state.pinkB2 = 0.57000f * state.pinkB2 + sample * 1.0526913f;
               sample = state.pinkB0 + state.pinkB1 + state.pinkB2 + sample * 0.1848f;
               sample *= 0.05f;
            }
            buffer[i] = sample * volume;
         }
      }

   } // namespace wasm
} // namespace bespoke
