#include "BespokeWasm/WasmAudioEngine.h"

#include "BespokeWasm/AudioHealth.h"
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
      std::chrono::steady_clock::time_point gLastCallbackTime{};
      bool gHasLastCallbackTime = false;
   }

   void processWasmAudio(const float* const* input, float* const* output,
                         int numInputChannels, int numOutputChannels, int numSamples)
   {
      (void)input;
      (void)numInputChannels;
      auto& state = runtimeState();
      const auto started = std::chrono::steady_clock::now();
      state.audioCallbackActive.store(true);

      double intervalSeconds = 0.0;
      if (gHasLastCallbackTime)
         intervalSeconds = std::chrono::duration<double>(started - gLastCallbackTime).count();
      gLastCallbackTime = started;
      gHasLastCallbackTime = true;

      auto finishMetrics = [&](int frames, float sampleRate)
      {
         const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count();
         const float bufferDuration = (sampleRate > 0.0f && frames > 0)
            ? static_cast<float>(frames) / sampleRate
            : 0.0f;
         const float instantaneous = bufferDuration > 0.0f ? elapsed / bufferDuration : 0.0f;
         gAudioCpuLoad.store(gAudioCpuLoad.load() * 0.9f + instantaneous * 0.1f);
         audioHealthOnCallback(elapsed, bufferDuration, intervalSeconds);
         state.audioCallbackActive.store(false);
      };

      if (numOutputChannels <= 0 || numSamples <= 0 || !output)
      {
         finishMetrics(0, 44100.0f);
         return;
      }

      if (!state.canvas || !state.audioGraphEngine)
      {
         for (int channel = 0; channel < std::min(numOutputChannels, 2); ++channel)
            if (output[channel])
               std::memset(output[channel], 0, numSamples * sizeof(float));
         finishMetrics(numSamples, 44100.0f);
         return;
      }

      const auto graph = state.canvas->getAudioGraphSnapshot();
      if (!graph)
      {
         for (int channel = 0; channel < std::min(numOutputChannels, 2); ++channel)
            if (output[channel])
               std::memset(output[channel], 0, numSamples * sizeof(float));
         finishMetrics(numSamples, 44100.0f);
         return;
      }

      float sampleRate = 44100.0f;
      if (state.externalAudioActive && state.externalSampleRate > 0.0f)
         sampleRate = state.externalSampleRate;
      else if (state.audioBackend)
         sampleRate = static_cast<float>(state.audioBackend->getSampleRate());

      state.audioGraphEngine->processBlock(*graph, output, numOutputChannels, numSamples, sampleRate);
      // Analysis is a single-producer ring write; never allocate or wait in the callback.
      AudioAnalysis::pushSamples(output[0], numSamples);
      finishMetrics(numSamples, sampleRate);
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
