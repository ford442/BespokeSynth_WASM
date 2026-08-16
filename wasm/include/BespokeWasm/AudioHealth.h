/**
 * BespokeSynth WASM - Audio health / underrun instrumentation
 */

#pragma once

#include <cstdint>

namespace bespoke::wasm
{

   struct AudioHealthSnapshot
   {
      uint64_t callbackCount = 0;
      uint64_t underrunCount = 0; // process exceeded buffer budget, or ring empty
      uint64_t externalUnderrunCount = 0; // reported by worklet/SAB consumer
      double lastCallbackIntervalMs = 0.0;
      double lastProcessTimeMs = 0.0;
      double maxProcessTimeMs = 0.0;
      float cpuLoad = 0.0f;
      int queueDepthFrames = 0;
      int maxQueueDepthFrames = 0;
      int capacityFrames = 0;
      int backend = 0; // 0=sdl, 1=worklet, 2=poc
      uint64_t noteDropCount = 0;
   };

   void audioHealthReset();
   void audioHealthOnCallback(double processSeconds, double bufferDurationSeconds, double intervalSeconds);
   void audioHealthSetBackend(int backend);
   void audioHealthSetQueueStats(int depthFrames, int capacityFrames, uint64_t externalUnderruns);
   void audioHealthSetNoteDropCount(uint64_t noteDropCount);
   void audioHealthRecordExternalUnderrun(uint64_t countDelta);
   AudioHealthSnapshot audioHealthSnapshot();
   /** JSON into a static buffer (valid until next call). */
   const char* audioHealthJson();

} // namespace bespoke::wasm
