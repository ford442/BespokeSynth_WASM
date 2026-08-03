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
#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>

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

         struct PulseEvent
         {
            double beatPosition = 0.0;
            float strength = 1.0f;
         };

         struct ModulationValue
         {
            float value = 0.0f;
         };

         void queueNote(const NoteMessage& note);
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
            FilterAdapterRuntimeState filterState;
            AdsrAdapterRuntimeState adsrState;
            DelayAdapterRuntimeState delayState;
            NoiseAdapterRuntimeState noiseState;
            StepSequencerRuntimeState sequencerState;
         };

         RuntimeState& stateFor(int moduleId);
         OscillatorType oscillatorTypeFor(int waveform) const;
         float renderOscillatorSample(RuntimeState& state,
                                      int waveform,
                                      float frequency,
                                      float sampleRate);
         float renderLfoSample(RuntimeState& state,
                               int shape,
                               float rate,
                               float depth,
                               float sampleRate);

         std::unordered_map<int, RuntimeState> mStates;
         std::vector<int> mProcessOrder;
         std::deque<NoteMessage> mNoteQueue;
         std::mutex mEventMutex;
         std::vector<PulseEvent> mPulseEvents;
         double mAudioTimeSeconds = 0.0;
         double mBeatPosition = 0.0;
      };

   } // namespace wasm
} // namespace bespoke
