#include "BespokeWasm/WasmAudioEngine.h"

#include "BespokeWasm/AudioHealth.h"
#include "BespokeWasm/AudioRtGuard.h"
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

      struct InterleavedScratch
      {
         std::vector<float> left;
         std::vector<float> right;

         void prepare(int frames)
         {
            if (static_cast<int>(left.size()) < frames)
            {
               left.resize(static_cast<size_t>(frames));
               right.resize(static_cast<size_t>(frames));
            }
         }
      };

      InterleavedScratch gInterleavedScratch;
   }

   void prepareWasmAudioScratch(int maxFrames)
   {
      gInterleavedScratch.prepare(maxFrames);
   }

   void processWasmAudio(const float* const* input, float* const* output,
                         int numInputChannels, int numOutputChannels, int numSamples)
   {
      (void)input;
      (void)numInputChannels;
      auto& state = runtimeState();
      const auto started = std::chrono::steady_clock::now();
      state.audioCallbackActive.store(true);
      gAudioCallbackActive.store(true, std::memory_order_release);

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
         if (state.audioGraphEngine)
            audioHealthSetNoteDropCount(state.audioGraphEngine->noteDropCount());
         state.audioCallbackActive.store(false);
         gAudioCallbackActive.store(false, std::memory_order_release);
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
      AudioAnalysis::pushSamples(output[0], numSamples);
      finishMetrics(numSamples, sampleRate);
   }

   void processWasmAudioInterleaved(float* output, int frames)
   {
      if (!output || frames <= 0)
         return;

      if (static_cast<int>(gInterleavedScratch.left.size()) < frames)
         return;

      float* channels[] = { gInterleavedScratch.left.data(), gInterleavedScratch.right.data() };
      processWasmAudio(nullptr, channels, 0, 2, frames);
      for (int frame = 0; frame < frames; ++frame)
      {
         output[frame * 2] = gInterleavedScratch.left[static_cast<size_t>(frame)];
         output[frame * 2 + 1] = gInterleavedScratch.right[static_cast<size_t>(frame)];
      }
   }

   float wasmAudioCpuLoad()
   {
      return gAudioCpuLoad.load();
   }
}
