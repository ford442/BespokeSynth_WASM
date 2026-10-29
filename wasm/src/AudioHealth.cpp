/**
 * BespokeSynth WASM - Audio health metrics (atomics-friendly for audio path)
 */

#include "BespokeWasm/AudioHealth.h"
#include "BespokeWasm/AudioRtGuard.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten/heap.h>
#endif

namespace bespoke::wasm
{
   namespace
   {
      std::atomic<uint64_t> gCallbackCount{ 0 };
      std::atomic<uint64_t> gUnderrunCount{ 0 };
      std::atomic<uint64_t> gExternalUnderrunCount{ 0 };
      std::atomic<double> gLastCallbackIntervalMs{ 0.0 };
      std::atomic<double> gLastProcessTimeMs{ 0.0 };
      std::atomic<double> gMaxProcessTimeMs{ 0.0 };
      std::atomic<float> gCpuLoad{ 0.0f };
      std::atomic<int> gQueueDepthFrames{ 0 };
      std::atomic<int> gMaxQueueDepthFrames{ 0 };
      std::atomic<int> gCapacityFrames{ 0 };
      std::atomic<int> gBackend{ 0 };
      std::atomic<uint64_t> gNoteDropCount{ 0 };
      std::atomic<uint64_t> gBlockTimeHistogram[kAudioBlockTimeBuckets]{};
      char gHealthJson[1024];

      int blockTimeBucket(double load)
      {
         constexpr double kUpperBounds[kAudioBlockTimeBuckets - 1] = { 0.1, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
         int bucket = 0;
         while (bucket < kAudioBlockTimeBuckets - 1 && load >= kUpperBounds[bucket])
            ++bucket;
         return bucket;
      }
   }

   void audioHealthReset()
   {
      gCallbackCount.store(0);
      gUnderrunCount.store(0);
      gExternalUnderrunCount.store(0);
      gLastCallbackIntervalMs.store(0.0);
      gLastProcessTimeMs.store(0.0);
      gMaxProcessTimeMs.store(0.0);
      gCpuLoad.store(0.0f);
      gQueueDepthFrames.store(0);
      gMaxQueueDepthFrames.store(0);
      gCapacityFrames.store(0);
      gNoteDropCount.store(0);
      for (auto& bucket : gBlockTimeHistogram)
         bucket.store(0);
   }

   void audioHealthOnCallback(double processSeconds, double bufferDurationSeconds, double intervalSeconds)
   {
      gCallbackCount.fetch_add(1);
      const double processMs = processSeconds * 1000.0;
      gLastProcessTimeMs.store(processMs);

      double prevMax = gMaxProcessTimeMs.load();
      while (processMs > prevMax &&
             !gMaxProcessTimeMs.compare_exchange_weak(prevMax, processMs))
      {
      }

      if (intervalSeconds > 0.0)
         gLastCallbackIntervalMs.store(intervalSeconds * 1000.0);

      if (bufferDurationSeconds > 0.0)
      {
         const float instantaneous = static_cast<float>(processSeconds / bufferDurationSeconds);
         gCpuLoad.store(gCpuLoad.load() * 0.9f + instantaneous * 0.1f);
         gBlockTimeHistogram[blockTimeBucket(processSeconds / bufferDurationSeconds)].fetch_add(1);
         if (processSeconds > bufferDurationSeconds * 1.05)
            gUnderrunCount.fetch_add(1);
      }
   }

   void audioHealthSetBackend(int backend)
   {
      gBackend.store(backend);
   }

   void audioHealthSetQueueStats(int depthFrames, int capacityFrames, uint64_t externalUnderruns)
   {
      const int depth = std::max(0, depthFrames);
      gQueueDepthFrames.store(depth);
      gCapacityFrames.store(std::max(0, capacityFrames));
      gExternalUnderrunCount.store(externalUnderruns);

      int prevMax = gMaxQueueDepthFrames.load();
      while (depth > prevMax &&
             !gMaxQueueDepthFrames.compare_exchange_weak(prevMax, depth))
      {
      }
   }

   void audioHealthSetNoteDropCount(uint64_t noteDropCount)
   {
      gNoteDropCount.store(noteDropCount);
   }

   void audioHealthRecordExternalUnderrun(uint64_t countDelta)
   {
      gExternalUnderrunCount.fetch_add(countDelta);
      gUnderrunCount.fetch_add(countDelta);
   }

   AudioHealthSnapshot audioHealthSnapshot()
   {
      AudioHealthSnapshot snap;
      snap.callbackCount = gCallbackCount.load();
      snap.underrunCount = gUnderrunCount.load();
      snap.externalUnderrunCount = gExternalUnderrunCount.load();
      snap.lastCallbackIntervalMs = gLastCallbackIntervalMs.load();
      snap.lastProcessTimeMs = gLastProcessTimeMs.load();
      snap.maxProcessTimeMs = gMaxProcessTimeMs.load();
      snap.cpuLoad = gCpuLoad.load();
      snap.queueDepthFrames = gQueueDepthFrames.load();
      snap.maxQueueDepthFrames = gMaxQueueDepthFrames.load();
      snap.capacityFrames = gCapacityFrames.load();
      snap.backend = gBackend.load();
      snap.noteDropCount = gNoteDropCount.load();
      for (int i = 0; i < kAudioBlockTimeBuckets; ++i)
         snap.blockTimeHistogram[i] = gBlockTimeHistogram[i].load();
#ifdef __EMSCRIPTEN__
      // Linear memory only grows, so the current size is the peak heap size.
      snap.heapBytes = static_cast<uint64_t>(emscripten_get_heap_size());
#endif
      return snap;
   }

   const char* audioHealthJson()
   {
      const AudioHealthSnapshot snap = audioHealthSnapshot();
      const AudioRtViolations rt = audioRtViolations();
      const auto* h = snap.blockTimeHistogram;
      std::snprintf(
         gHealthJson, sizeof(gHealthJson),
         "{\"backend\":%d,\"callbackCount\":%llu,\"underrunCount\":%llu,"
         "\"externalUnderrunCount\":%llu,\"noteDropCount\":%llu,\"lastCallbackIntervalMs\":%.3f,"
         "\"lastProcessTimeMs\":%.3f,\"maxProcessTimeMs\":%.3f,\"cpuLoad\":%.4f,"
         "\"queueDepthFrames\":%d,\"maxQueueDepthFrames\":%d,\"capacityFrames\":%d,"
         "\"heapBytes\":%llu,\"blockTimeHistogram\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu],"
         "\"rtAllocations\":%llu,\"rtFrees\":%llu,\"rtLocks\":%llu,\"rtCountsAllocations\":%s}",
         snap.backend,
         static_cast<unsigned long long>(snap.callbackCount),
         static_cast<unsigned long long>(snap.underrunCount),
         static_cast<unsigned long long>(snap.externalUnderrunCount),
         static_cast<unsigned long long>(snap.noteDropCount),
         snap.lastCallbackIntervalMs,
         snap.lastProcessTimeMs,
         snap.maxProcessTimeMs,
         snap.cpuLoad,
         snap.queueDepthFrames,
         snap.maxQueueDepthFrames,
         snap.capacityFrames,
         static_cast<unsigned long long>(snap.heapBytes),
         static_cast<unsigned long long>(h[0]),
         static_cast<unsigned long long>(h[1]),
         static_cast<unsigned long long>(h[2]),
         static_cast<unsigned long long>(h[3]),
         static_cast<unsigned long long>(h[4]),
         static_cast<unsigned long long>(h[5]),
         static_cast<unsigned long long>(h[6]),
         static_cast<unsigned long long>(h[7]),
         static_cast<unsigned long long>(rt.allocations),
         static_cast<unsigned long long>(rt.frees),
         static_cast<unsigned long long>(rt.locks),
         rt.countsAllocations ? "true" : "false");
      return gHealthJson;
   }
}
