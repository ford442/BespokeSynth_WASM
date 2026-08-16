/**
 * BespokeSynth WASM - Audio health metrics (atomics-friendly for audio path)
 */

#include "BespokeWasm/AudioHealth.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

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
      char gHealthJson[576];
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
      return snap;
   }

   const char* audioHealthJson()
   {
      const AudioHealthSnapshot snap = audioHealthSnapshot();
      std::snprintf(
         gHealthJson, sizeof(gHealthJson),
         "{\"backend\":%d,\"callbackCount\":%llu,\"underrunCount\":%llu,"
         "\"externalUnderrunCount\":%llu,\"noteDropCount\":%llu,\"lastCallbackIntervalMs\":%.3f,"
         "\"lastProcessTimeMs\":%.3f,\"maxProcessTimeMs\":%.3f,\"cpuLoad\":%.4f,"
         "\"queueDepthFrames\":%d,\"maxQueueDepthFrames\":%d,\"capacityFrames\":%d}",
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
         snap.capacityFrames);
      return gHealthJson;
   }
}
