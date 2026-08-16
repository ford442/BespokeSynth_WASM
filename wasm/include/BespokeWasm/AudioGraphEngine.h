/**
 * BespokeSynth WASM - Audio graph processor
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include "BespokeWasm/adapters/AdsrModuleAdapter.h"
#include "BespokeWasm/adapters/DelayModuleAdapter.h"
#include "BespokeWasm/adapters/FilterModuleAdapter.h"
#include "BespokeWasm/adapters/NoiseModuleAdapter.h"
#include "BespokeWasm/adapters/StepSequencerModuleAdapter.h"
#include "Oscillator.h"
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

         void queueNote(const NoteMessage& note);
         uint64_t noteDropCount() const { return mNoteDropCount.load(std::memory_order_relaxed); }

         /** Grow buffer arenas on the UI thread (init / buffer-size changes). */
         void prepareForBlock(int maxBlockSize, int maxAudioSlots, int maxModulationSlots);

         void processBlock(const AudioGraphSnapshot& graph,
                           float* const* output,
                           int numOutputChannels,
                           int numSamples,
                           float sampleRate);

      private:
         struct RuntimeState
         {
            float phase = 0.0f;
            float noteFrequency = 440.0f;
            float noteVelocity = 1.0f;
            bool noteGate = true;
            bool hasReceivedNote = false;
            OscillatorType oscillatorType = kOsc_Sin;
            Oscillator oscillator{kOsc_Sin};
            FilterAdapterRuntimeState filterState;
            AdsrAdapterRuntimeState adsrState;
            DelayAdapterRuntimeState delayState;
            NoiseAdapterRuntimeState noiseState;
            StepSequencerRuntimeState sequencerState;
         };

         RuntimeState& stateFor(int moduleId);
         void updateOscillatorType(RuntimeState& state, int waveform);
         float renderOscillatorSample(RuntimeState& state,
                                      float frequency,
                                      float sampleRate);
         float renderLfoSample(RuntimeState& state,
                               int shape,
                               float rate,
                               float depth,
                               float sampleRate);
         void drainNoteRing();
         float* audioSlotBuffer(int slot, int numSamples);
         float* modulationSlotBuffer(int slot, int numSamples);

         std::unordered_map<int, RuntimeState> mStates;
         std::vector<float> mAudioArena;
         std::vector<float> mModulationArena;
         std::vector<float> mSummedModulation;
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
