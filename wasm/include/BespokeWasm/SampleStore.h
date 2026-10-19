/**
 * BespokeSynth WASM - UI-thread sample store
 *
 * The audio thread only ever observes const SampleBuffer* values published
 * through an AudioGraphSnapshot. Buffers are retained for the session.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/SampleBuffer.h"
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class SampleStore
      {
      public:
         static SampleStore& instance();

         int loadFromMemory(const uint8_t* bytes, int length, const char* name, int deviceSampleRate);
         const SampleBuffer* findById(int id) const;
         const SampleBuffer* findByHash(const std::string& hash) const;
         /** Ids are dense and never reused, so this is also one past the newest id. */
         int count() const;
         /**
          * AudioWorklet instance: take a buffer the UI instance decoded, under the UI's id, so
          * SamplerParams::sampleId resolves to the same PCM in both heaps. Sets the buffer's id
          * and replaces any buffer already at `id`.
          */
         void adopt(int id, std::shared_ptr<SampleBuffer> buffer);
         void clear();

         /**
          * Rate the engine is running at. Samples are resampled to it when they load (the
          * sampler does not rate-convert), so loads that don't come from a caller who knows
          * the rate - the bundled kit - read it from here.
          */
         void setDeviceSampleRate(int sampleRate)
         {
            if (sampleRate > 0)
               mDeviceSampleRate.store(sampleRate, std::memory_order_relaxed);
         }
         int deviceSampleRate() const { return mDeviceSampleRate.load(std::memory_order_relaxed); }

      private:
         SampleStore() = default;

         mutable std::mutex mMutex;
         std::vector<SampleBufferPtr> mById;
         std::unordered_map<std::string, int> mIdByHash;
         std::atomic<int> mDeviceSampleRate{ 44100 };
      };

   } // namespace wasm
} // namespace bespoke
