/**
 * BespokeSynth WASM - Audio graph processor
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/ModuleCanvas.h"
#include "BiquadFilter.h"
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
         // Minimal WASM equivalents of desktop INoteReceiver/IPulseReceiver payloads.
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

         // Block-rate values can be extended to a per-sample buffer by consumers.
         struct ModulationValue
         {
            float value = 0.0f;
         };

         void queueNote(const NoteMessage& note);
         void processBlock(const ModuleCanvas::AudioGraphSnapshot& graph,
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
            BiquadFilter filter;
            float lastSampleRate = 0.0f;
            int lastFilterType = -1;
         };

         RuntimeState& stateFor(int moduleId);
         OscillatorType oscillatorTypeFor(int waveform) const;
         FilterType filterTypeFor(int filterType) const;
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
