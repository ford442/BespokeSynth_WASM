/**
 * BespokeSynth WASM - Audio graph processor
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <atomic>
#include <cstdint>
#include <memory>
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
            int channel = 0;
         };

         static constexpr int kMaxNoteRingCapacity = 256;
         static constexpr int kMaxNoteEventsPerBlock = 64;
         static constexpr int kMaxEventSources = 16;
         static constexpr int kMaxBlockSize = 2048;

         AudioGraphEngine();
         ~AudioGraphEngine();

         AudioGraphEngine(const AudioGraphEngine&) = delete;
         AudioGraphEngine& operator=(const AudioGraphEngine&) = delete;

         void queueNote(const NoteMessage& note);
         uint64_t noteDropCount() const { return mNoteDropCount.load(std::memory_order_relaxed); }

         /** Grow buffer arenas on the UI thread (init / buffer-size changes). */
         void prepareForBlock(int maxBlockSize, int maxAudioSlots, int maxModulationSlots);

         /** Zero transport clock and drop queued MIDI (offline / determinism). */
         void resetClock();

         /**
          * UI thread, after each snapshot publish: create runtime state for new
          * nodes and reset state of nodes that left the graph, so spawn/delete/
          * undo never grow arenas inside processBlock. Assumes the audio graph
          * is not running concurrently (main-thread audio; BESPOKE_WASM_THREADS=OFF).
          */
         void prepareRuntimeStates(const AudioGraphSnapshot& graph);

         /** Transport position in beats at the end of the last processed block (0 when stopped). */
         double beatPosition() const { return mPublishedBeat.load(std::memory_order_relaxed); }

         /**
          * Largest |sample| reaching the Output module since the last call, per
          * channel (0 = L, 1 = R). Measured on the summed input, before the
          * Output limiter, so values above 1 mean the patch is clipping.
          * Single reader (UI thread); resets on read.
          */
         float takeOutputPeak(int channel);

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
            /** Module left the graph; state was reset in place for a possible undo. */
            bool dormant = false;
         };

         void* stateFor(const AudioGraphNode& node);
         void destroySlot(RuntimeSlot& slot);
         void resetEventSourceState();
         void drainNoteRing();
         /** Planar slot: channel 0 at slot * 2 * block, channel 1 one block later. */
         float* audioSlotBuffer(int slot, int channel, int numSamples);
         int audioSlotChannels(int slot) const;
         float* modulationSlotBuffer(int slot, int numSamples);
         void publishOutputPeaks(const float* const* channels, int numChannels, int numSamples);

         struct ProcessScratch;

         std::vector<uint8_t> mRuntimeArena;
         std::vector<RuntimeSlot> mRuntimeSlots;
         std::unordered_map<int, int> mSlotIndexByModuleId;
         std::vector<float> mAudioArena;
         /** Channels written to each audio slot this block (1 = mono, read R as L). */
         std::vector<uint8_t> mSlotChannels;
         std::vector<float> mModulationArena;
         std::vector<float> mSummedModulation;
         std::vector<float> mInputScratch;
         int mPreparedBlockSize = 0;
         int mPreparedAudioSlots = 0;
         int mPreparedModulationSlots = 0;

         std::unique_ptr<ProcessScratch> mScratch;
         std::atomic<uint32_t> mNoteWriteIndex{ 0 };
         std::atomic<uint32_t> mNoteReadIndex{ 0 };
         std::atomic<uint64_t> mNoteDropCount{ 0 };

         int mMidiNoteCount = 0;

         double mAudioTimeSeconds = 0.0;
         double mBeatPosition = 0.0;
         std::atomic<double> mPublishedBeat{ 0.0 };
         std::atomic<float> mOutputPeak[2]{ { 0.0f }, { 0.0f } };
      };

   } // namespace wasm
} // namespace bespoke
