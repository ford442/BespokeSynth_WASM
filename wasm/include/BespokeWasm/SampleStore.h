/**
 * BespokeSynth WASM - UI-thread sample store
 *
 * The audio thread only ever observes const SampleBuffer* values published
 * through an AudioGraphSnapshot. Buffers are immutable once loaded. Each id
 * (one per content hash, never reused) holds at most one resident buffer;
 * a reload after eviction gives it a new generation, so a SampleHandle names
 * exactly one buffer. Eviction is deferred: a buffer is only freed once no
 * snapshot the engine may still process refers to it (retainOnly / collect).
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioRtGuard.h"
#include "BespokeWasm/SampleBuffer.h"
#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      /** A resident buffer: its store id and the generation it was loaded as (0 = none). */
      struct SampleHandle
      {
         int32_t index = -1;
         uint32_t generation = 0;

         bool valid() const { return index >= 0 && generation != 0; }
         friend bool operator==(const SampleHandle&, const SampleHandle&) = default;
      };

      class SampleStore
      {
      public:
         static SampleStore& instance();

         /** Decodes (or finds) the sample; returns its id. An evicted id is reloaded under a new generation. */
         int loadFromMemory(const uint8_t* bytes, int length, const char* name, int deviceSampleRate);
         /** Resident buffer of `id`, or nullptr. */
         const SampleBuffer* findById(int id) const;
         /** Resident buffer of `hash`, or nullptr (evicted or never loaded). */
         const SampleBuffer* findByHash(const std::string& hash) const;
         [[nodiscard]] SampleHandle handleFor(int id) const;
         /** The buffer `handle` names, or nullptr once it was evicted or replaced. */
         [[nodiscard]] const SampleBuffer* resolve(SampleHandle handle) const;
         /** Generation of the resident buffer of `id` (0 = not resident). */
         [[nodiscard]] uint32_t generationOf(int id) const;
         /** Ids are dense and never reused, so this is also one past the newest id. */
         int count() const;
         /**
          * AudioWorklet instance: take a buffer the UI instance decoded, under the UI's id and
          * generation, so a SampleHandle resolves to the same PCM in both heaps. Sets the buffer's
          * id and replaces any buffer already at `id`. Generation 0 assigns a fresh one.
          */
         void adopt(int id, std::shared_ptr<SampleBuffer> buffer, uint32_t generation = 0);
         /** AudioWorklet instance: the UI instance evicted `id`; drop this heap's copy. */
         void evict(int id);
         void clear();

         /**
          * After publishing snapshot `generation`: `referencedIds` are pinned; every other
          * resident buffer is unpinned as of `generation` (a new load stays pinned until a
          * snapshot that does not name it).
          */
         void retainOnly(std::span<const int> referencedIds, uint64_t generation);
         /**
          * The least recently referenced unpinned buffer that was unpinned at or before
          * `safeGeneration` (the oldest snapshot the engine may still process). Sets
          * `lastUse` and returns its id, or -1.
          */
         int evictionCandidate(uint64_t safeGeneration, uint64_t& lastUse) const;
         /** Frees the buffer of `id`; returns the bytes released. */
         size_t evictId(int id);
         size_t residentBytes() const;

         /** A sampler wanted `hash` and it is not resident; the host is asked to load it again. */
         void noteMissing(const std::string& hash);
         std::vector<std::string> takeMissing();
         bool hasMissing() const;

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

         struct Slot
         {
            SampleBufferPtr buffer;
            std::string hash;
            uint32_t generation = 0;
            bool pinned = true;
            uint64_t unpinnedAt = 0;
            uint64_t lastUse = 0;
         };

         static size_t bytesOf(const SampleBuffer& buffer);
         void installLocked(int id, SampleBufferPtr buffer, uint32_t generation);

         mutable RtMutex mMutex;
         std::vector<Slot> mSlots;
         std::unordered_map<std::string, int> mIdByHash;
         std::vector<std::string> mMissing;
         uint32_t mNextGeneration = 1;
         uint64_t mLastRetainGeneration = 0;
         size_t mResidentBytes = 0;
         std::atomic<int> mDeviceSampleRate{ 44100 };
      };

   } // namespace wasm
} // namespace bespoke
