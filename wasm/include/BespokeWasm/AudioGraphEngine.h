/**
 * BespokeSynth WASM - Audio graph processor
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class AudioGraphEngine
      {
      public:
         struct NoteMessage
         {
            int pitch = 60;
            float velocity = 0.0f;
            bool isNoteOn = false;
            double timestamp = 0.0;
         };

         static constexpr int kMaxNoteRingCapacity = 256;
         static constexpr int kMaxNoteEventsPerBlock = 64;
         static constexpr int kMaxBlockSize = 2048;

         AudioGraphEngine() = default;
         ~AudioGraphEngine();

         AudioGraphEngine(const AudioGraphEngine&) = delete;
         AudioGraphEngine& operator=(const AudioGraphEngine&) = delete;

         void queueNote(const NoteMessage& note);
         uint64_t noteDropCount() const { return mNoteDropCount.load(std::memory_order_relaxed); }

         /** Grow buffer arenas on the UI thread (init / buffer-size changes). */
         void prepareForBlock(int maxBlockSize, int maxAudioSlots, int maxModulationSlots);

         /** Zero transport clock and drop queued MIDI (offline / determinism). */
         void resetClock();

         void processBlock(const AudioGraphSnapshot& graph,
                           float* const* output,
                           int numOutputChannels,
                           int numSamples,
                           float sampleRate);

      private:
         struct RuntimeSlot
         {
            int moduleId = -1;
            int typeIndex = -1;
            uint32_t offset = 0;
            uint32_t size = 0;
         };

         void* stateFor(const AudioGraphNode& node);
         void destroySlot(RuntimeSlot& slot);
         void resetNoteSourceState();
         void drainNoteRing();
         float* audioSlotBuffer(int slot, int numSamples);
         float* modulationSlotBuffer(int slot, int numSamples);

         std::vector<uint8_t> mRuntimeArena;
         std::vector<RuntimeSlot> mRuntimeSlots;
         std::unordered_map<int, int> mSlotIndexByModuleId;
         std::vector<float> mAudioArena;
         std::vector<float> mModulationArena;
         std::vector<float> mSummedModulation;
         std::vector<float> mInputScratch;
         int mPreparedBlockSize = 0;
         int mPreparedAudioSlots = 0;
         int mPreparedModulationSlots = 0;

         std::array<NoteMessage, kMaxNoteRingCapacity> mNoteRing{};
         std::atomic<uint32_t> mNoteWriteIndex{ 0 };
         std::atomic<uint32_t> mNoteReadIndex{ 0 };
         std::atomic<uint64_t> mNoteDropCount{ 0 };

         std::array<WasmNoteEvent, kMaxNoteEventsPerBlock> mMidiNoteScratch{};
         int mMidiNoteCount = 0;
         std::array<std::array<WasmNoteEvent, kMaxNoteEventsPerBlock>, 16> mNoteSourceScratch{};
         std::array<int, 16> mNoteSourceCounts{};

         double mAudioTimeSeconds = 0.0;
         double mBeatPosition = 0.0;
      };

   } // namespace wasm
} // namespace bespoke
