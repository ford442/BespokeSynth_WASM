/**
 * BespokeSynth WASM - Offline graph render
 */

#include "BespokeWasm/OfflineRender.h"
#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/AudioProcessPlan.h"
#include "BespokeWasm/SampleBuffer.h"
#include <algorithm>
#include <cmath>

namespace bespoke
{
   namespace wasm
   {
      bool renderGraphOfflinePcm(const AudioGraphSnapshot& graphIn,
                                 double seconds,
                                 int sampleRate,
                                 std::vector<float>& interleavedStereo,
                                 std::string& error)
      {
         interleavedStereo.clear();
         if (seconds <= 0.0 || sampleRate <= 0)
         {
            error = "Invalid offline render duration or sample rate";
            return false;
         }

         AudioGraphSnapshot graph = graphIn;
         graph.transportPlaying = true;
         if (graph.transportBPM <= 0.0f)
            graph.transportBPM = 120.0f;
         compileAudioProcessPlan(graph, graph.processPlan);
         if (!graph.processPlan.valid || graph.processPlan.hasCycle)
         {
            error = "Audio graph is not renderable";
            return false;
         }

         const int frames = std::max(1, static_cast<int>(std::llround(seconds * sampleRate)));
         const int block = 128;
         AudioGraphEngine engine;
         engine.prepareForBlock(block,
                                std::max(8, graph.processPlan.bufferSlotCount + 4),
                                std::max(4, graph.processPlan.modulationSlotCount + 2));
         engine.resetClock();

         interleavedStereo.resize(static_cast<size_t>(frames) * 2u, 0.0f);
         std::vector<float> left(static_cast<size_t>(block));
         std::vector<float> right(static_cast<size_t>(block));
         float* channels[2] = { left.data(), right.data() };

         int written = 0;
         while (written < frames)
         {
            const int n = std::min(block, frames - written);
            engine.processBlock(graph, channels, 2, n, static_cast<float>(sampleRate));
            for (int i = 0; i < n; ++i)
            {
               interleavedStereo[static_cast<size_t>((written + i) * 2)] = left[static_cast<size_t>(i)];
               interleavedStereo[static_cast<size_t>((written + i) * 2 + 1)] = right[static_cast<size_t>(i)];
            }
            written += n;
         }
         return true;
      }

      bool renderGraphOffline(const AudioGraphSnapshot& graph,
                              double seconds,
                              int sampleRate,
                              int bitsPerSample,
                              std::vector<uint8_t>& wavOut,
                              std::string& error)
      {
         std::vector<float> pcm;
         if (!renderGraphOfflinePcm(graph, seconds, sampleRate, pcm, error))
            return false;
         const int frames = static_cast<int>(pcm.size() / 2);
         const int bits = (bitsPerSample == 24 || bitsPerSample == 16) ? bitsPerSample : 32;
         return encodeWav(pcm.data(), frames, 2, sampleRate, bits, wavOut, error);
      }

   } // namespace wasm
} // namespace bespoke
