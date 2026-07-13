#include "BespokeWasm/WasmAudioEngine.h"

#include "BespokeWasm/WasmRuntimeState.h"
#include "BespokeWasm/AudioAnalysis.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace bespoke::wasm
{
   namespace
   {
      std::atomic<float> gAudioCpuLoad{ 0.0f };
   }

   void processWasmAudio(const float* const* input, float* const* output,
                         int numInputChannels, int numOutputChannels, int numSamples)
   {
      (void)input;
      (void)numInputChannels;
      auto& state = runtimeState();
      const auto started = std::chrono::steady_clock::now();
      state.audioCallbackActive.store(true);

      if (numOutputChannels <= 0 || numSamples <= 0 || !output)
      {
         state.audioCallbackActive.store(false);
         return;
      }

      if (!state.canvas || !state.audioGraphEngine)
      {
         for (int channel = 0; channel < std::min(numOutputChannels, 2); ++channel)
            if (output[channel])
               std::memset(output[channel], 0, numSamples * sizeof(float));
         state.audioCallbackActive.store(false);
         return;
      }

      const auto graph = state.canvas->createAudioGraphSnapshot();
      const float sampleRate = state.audioBackend
         ? static_cast<float>(state.audioBackend->getSampleRate())
         : 44100.0f;
      state.audioGraphEngine->processBlock(graph, output, numOutputChannels, numSamples, sampleRate);
      // Analysis is a single-producer ring write; never allocate or wait in the callback.
      AudioAnalysis::pushSamples(output[0], numSamples);
      const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count();
      const float instantaneous = sampleRate > 0.0f ? elapsed / (numSamples / sampleRate) : 0.0f;
      gAudioCpuLoad.store(gAudioCpuLoad.load() * 0.9f + instantaneous * 0.1f);
      state.audioCallbackActive.store(false);
   }

   void processWasmAudioInterleaved(float* output, int frames)
   {
      if (!output || frames <= 0)
         return;
      std::vector<float> left(frames, 0.0f);
      std::vector<float> right(frames, 0.0f);
      float* channels[] = { left.data(), right.data() };
      processWasmAudio(nullptr, channels, 0, 2, frames);
      for (int frame = 0; frame < frames; ++frame)
      {
         output[frame * 2] = left[frame];
         output[frame * 2 + 1] = right[frame];
      }
   }

   float wasmAudioCpuLoad()
   {
      return gAudioCpuLoad.load();
   }
}
