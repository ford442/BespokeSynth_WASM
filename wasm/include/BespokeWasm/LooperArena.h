/**
 * BespokeSynth WASM - Preallocated looper record arenas (UI-thread owned)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      struct LooperArena
      {
         static constexpr int kMaxFrames = 16 * 48000;

         /** Planar: L in [0, kMaxFrames), R in [kMaxFrames, 2 * kMaxFrames). */
         std::vector<float> samples;
         /** Width of the recorded loop (1 = only the L plane is meaningful). */
         int channels = 1;
         int loopLength = 0;
         int writePos = 0;
         int playPos = 0;
         int state = 0; // 0 idle, 1 waiting, 2 recording, 3 playing, 4 overdub
         int pendingMode = 0; // 1 record, 2 overdub
         double queuedBarBeat = 0.0;
         bool hasLoop = false;

         LooperArena()
         : samples(static_cast<size_t>(kMaxFrames) * 2u, 0.0f)
         {
         }

         float* plane(int channel) { return samples.data() + static_cast<size_t>(channel) * kMaxFrames; }
      };

      /**
       * One arena per looper module id, created on the UI thread (ensure). Arenas follow the
       * SampleStore lifetime rules: retainOnly unpins the ones no published snapshot names, and
       * an unpinned arena is only freed (evictId) once the engine is past that snapshot. Until
       * then an undo of the delete gets the same arena, recording included.
       */
      class LooperArenaPool
      {
      public:
         static LooperArenaPool& instance();

         LooperArena* ensure(int moduleId);
         LooperArena* find(int moduleId);
         /** Frees the arena of `moduleId` now. Only when nothing can process it (tests, resets). */
         void release(int moduleId);

         void retainOnly(std::span<const int> referencedModuleIds, uint64_t generation);
         /** Like SampleStore::evictionCandidate. */
         int evictionCandidate(uint64_t safeGeneration, uint64_t& lastUse) const;
         size_t evictId(int moduleId);
         size_t residentBytes() const;

         static constexpr size_t kArenaBytes = static_cast<size_t>(LooperArena::kMaxFrames) * 2u * sizeof(float);

      private:
         LooperArenaPool() = default;

         struct Slot
         {
            std::unique_ptr<LooperArena> arena;
            bool pinned = true;
            uint64_t unpinnedAt = 0;
            uint64_t lastUse = 0;
         };

         std::vector<Slot> mSlots;
         uint64_t mLastRetainGeneration = 0;
      };

   } // namespace wasm
} // namespace bespoke
