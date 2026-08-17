/**
 * BespokeSynth WASM - Preallocated looper record arenas (UI-thread owned)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <memory>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      struct LooperArena
      {
         static constexpr int kMaxFrames = 16 * 48000;

         std::vector<float> samples;
         int loopLength = 0;
         int writePos = 0;
         int playPos = 0;
         int state = 0; // 0 idle, 1 waiting, 2 recording, 3 playing, 4 overdub
         int pendingMode = 0; // 1 record, 2 overdub
         double queuedBarBeat = 0.0;
         bool hasLoop = false;

         LooperArena()
         : samples(static_cast<size_t>(kMaxFrames), 0.0f)
         {
         }
      };

      class LooperArenaPool
      {
      public:
         static LooperArenaPool& instance();

         LooperArena* ensure(int moduleId);
         LooperArena* find(int moduleId);
         void release(int moduleId);

      private:
         LooperArenaPool() = default;

         std::vector<std::unique_ptr<LooperArena>> mArenas;
      };

   } // namespace wasm
} // namespace bespoke
