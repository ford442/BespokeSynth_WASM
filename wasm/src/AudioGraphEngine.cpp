/**
 * BespokeSynth WASM - Audio graph processor
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/adapters/AdsrModuleAdapter.h"
#include "BespokeWasm/adapters/DelayModuleAdapter.h"
#include "BespokeWasm/adapters/FilterModuleAdapter.h"
#include "BespokeWasm/adapters/NoiseModuleAdapter.h"
#include "BespokeWasm/adapters/StepSequencerModuleAdapter.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr float kTwoPi = 6.28318530717958647692f;

         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }
      }

      void AudioGraphEngine::prepareForBlock(int maxBlockSize, int maxAudioSlots, int maxModulationSlots)
      {
         maxBlockSize = std::max(1, std::min(maxBlockSize, kMaxBlockSize));
         maxAudioSlots = std::max(1, maxAudioSlots);
         maxModulationSlots = std::max(0, maxModulationSlots);

         mPreparedBlockSize = maxBlockSize;
         mPreparedAudioSlots = maxAudioSlots;
         mPreparedModulationSlots = maxModulationSlots;

         const size_t audioSamples = static_cast<size_t>(maxAudioSlots) * static_cast<size_t>(maxBlockSize);
         if (mAudioArena.size() < audioSamples)
            mAudioArena.resize(audioSamples);

         const size_t modSamples = static_cast<size_t>(maxModulationSlots) * static_cast<size_t>(maxBlockSize);
         if (mModulationArena.size() < modSamples)
            mModulationArena.resize(modSamples);

         if (mSummedModulation.size() < static_cast<size_t>(maxBlockSize))
            mSummedModulation.resize(static_cast<size_t>(maxBlockSize));
      }

      AudioGraphEngine::RuntimeState& AudioGraphEngine::stateFor(int moduleId)
      {
         return mStates[moduleId];
      }

      void AudioGraphEngine::queueNote(const NoteMessage& note)
      {
         const uint32_t write = mNoteWriteIndex.load(std::memory_order_relaxed);
         const uint32_t nextWrite = (write + 1) % kMaxNoteRingCapacity;
         const uint32_t read = mNoteReadIndex.load(std::memory_order_acquire);
         if (nextWrite == read)
         {
            mNoteReadIndex.store((read + 1) % kMaxNoteRingCapacity, std::memory_order_release);
            mNoteDropCount.fetch_add(1, std::memory_order_relaxed);
         }
         mNoteRing[write] = note;
         mNoteWriteIndex.store(nextWrite, std::memory_order_release);
      }

      void AudioGraphEngine::drainNoteRing()
      {
         mMidiNoteCount = 0;
         while (mMidiNoteCount < kMaxNoteEventsPerBlock)
         {
            const uint32_t read = mNoteReadIndex.load(std::memory_order_relaxed);
            const uint32_t write = mNoteWriteIndex.load(std::memory_order_acquire);
            if (read == write)
               break;

            const NoteMessage& note = mNoteRing[read];
            mMidiNoteScratch[static_cast<size_t>(mMidiNoteCount++)] = {
               note.pitch, note.velocity, note.isNoteOn
            };
            mNoteReadIndex.store((read + 1) % kMaxNoteRingCapacity, std::memory_order_release);
         }
      }

      float* AudioGraphEngine::audioSlotBuffer(int slot, int numSamples)
      {
         if (slot < 0 || slot >= mPreparedAudioSlots || numSamples > mPreparedBlockSize)
            return nullptr;
         return mAudioArena.data() + static_cast<size_t>(slot) * static_cast<size_t>(mPreparedBlockSize);
      }

      float* AudioGraphEngine::modulationSlotBuffer(int slot, int numSamples)
      {
         if (slot < 0 || slot >= mPreparedModulationSlots || numSamples > mPreparedBlockSize)
            return nullptr;
         return mModulationArena.data() + static_cast<size_t>(slot) * static_cast<size_t>(mPreparedBlockSize);
      }

      void AudioGraphEngine::updateOscillatorType(RuntimeState& state, int waveform)
      {
         switch (waveform % 4)
         {
            case 1:
               state.oscillatorType = kOsc_Saw;
               break;
            case 2:
               state.oscillatorType = kOsc_Square;
               break;
            case 3:
               state.oscillatorType = kOsc_Tri;
               break;
            default:
               state.oscillatorType = kOsc_Sin;
               break;
         }
         state.oscillator.SetType(state.oscillatorType);
      }

      float AudioGraphEngine::renderOscillatorSample(RuntimeState& state,
                                                     float frequency,
                                                     float sampleRate)
      {
         const float sample = state.oscillator.Value(state.phase);
         state.phase += kTwoPi * clampFloat(frequency, 0.0f, sampleRate * 0.45f) / sampleRate;
         if (state.phase >= kTwoPi)
            state.phase = std::fmod(state.phase, kTwoPi);
         return sample;
      }

      float AudioGraphEngine::renderLfoSample(RuntimeState& state,
                                              int shape,
                                              float rate,
                                              float depth,
                                              float sampleRate)
      {
         updateOscillatorType(state, shape);
         const float bipolar = renderOscillatorSample(state, rate, sampleRate);
         return bipolar * clampFloat(depth, 0.0f, 1.0f);
      }

      void AudioGraphEngine::processBlock(const AudioGraphSnapshot& graph,
                                          float* const* output,
                                          int numOutputChannels,
                                          int numSamples,
                                          float sampleRate)
      {
         if (!output || numOutputChannels <= 0 || numSamples <= 0)
            return;

         const int channels = std::min(numOutputChannels, 2);
         for (int ch = 0; ch < channels; ++ch)
         {
            if (output[ch])
               std::memset(output[ch], 0, static_cast<size_t>(numSamples) * sizeof(float));
         }

         if (!graph.transportPlaying || sampleRate <= 0.0f)
         {
            if (!graph.transportPlaying)
            {
               mBeatPosition = 0.0;
               for (auto& [id, state] : mStates)
               {
                  (void)id;
                  state.sequencerState = StepSequencerRuntimeState{};
               }
            }
            return;
         }

         const AudioProcessPlan& plan = graph.processPlan;
         if (!plan.valid || plan.hasCycle)
            return;

         if (numSamples > mPreparedBlockSize ||
             plan.bufferSlotCount > mPreparedAudioSlots ||
             plan.modulationSlotCount > mPreparedModulationSlots)
            return;

         drainNoteRing();

         const double beatStart = mBeatPosition;
         const double beatsPerBlock = static_cast<double>(numSamples) * graph.transportBPM /
            (static_cast<double>(sampleRate) * 60.0);
         const double beatEnd = beatStart + beatsPerBlock;
         mBeatPosition = beatEnd;

         static const FilterModuleAdapter kFilterAdapter;
         static const AdsrModuleAdapter kAdsrAdapter;
         static const DelayModuleAdapter kDelayAdapter;
         static const NoiseModuleAdapter kNoiseAdapter;
         static const StepSequencerModuleAdapter kSequencerAdapter;

         mNoteSourceCounts.fill(0);
         for (int seqSlot = 0; seqSlot < static_cast<int>(plan.sequencerNodeIndices.size()); ++seqSlot)
         {
            const int seqNodeIndex = plan.sequencerNodeIndices[static_cast<size_t>(seqSlot)];
            const auto& node = graph.nodes[static_cast<size_t>(seqNodeIndex)];
            int& noteCount = mNoteSourceCounts[static_cast<size_t>(seqSlot)];
            noteCount = 0;
            auto& state = stateFor(node.id);
            kSequencerAdapter.emitNotesForBeatRange(
               &state.sequencerState,
               node,
               beatStart,
               beatEnd,
               mNoteSourceScratch[static_cast<size_t>(seqSlot)].data(),
               kMaxNoteEventsPerBlock,
               noteCount);
         }

         for (int lfoSlot = 0; lfoSlot < static_cast<int>(plan.lfoNodeIndices.size()); ++lfoSlot)
         {
            const int lfoNodeIndex = plan.lfoNodeIndices[static_cast<size_t>(lfoSlot)];
            const auto& node = graph.nodes[static_cast<size_t>(lfoNodeIndex)];
            float* buffer = modulationSlotBuffer(lfoSlot, numSamples);
            if (!buffer)
               continue;

            auto& state = stateFor(node.id);
            updateOscillatorType(state, node.lfoShape);
            for (int i = 0; i < numSamples; ++i)
               buffer[i] = renderLfoSample(state, node.lfoShape, node.lfoRate, node.lfoDepth, sampleRate);
         }

         const double blockStart = mAudioTimeSeconds;
         const WasmNoteEvent* globalMidiNotes =
            mMidiNoteCount > 0 ? mMidiNoteScratch.data() : nullptr;

         for (const auto& step : plan.steps)
         {
            const auto& node = graph.nodes[static_cast<size_t>(step.nodeIndex)];
            float* buffer = audioSlotBuffer(step.outBufferSlot, numSamples);
            if (!buffer)
               continue;

            if (step.outBufferSlot >= 0)
               std::memset(buffer, 0, static_cast<size_t>(numSamples) * sizeof(float));

            for (int inputSlot : step.inputBufferSlots)
            {
               const float* source = audioSlotBuffer(inputSlot, numSamples);
               if (!source)
                  continue;
               for (int i = 0; i < numSamples; ++i)
                  buffer[i] += source[i];
            }

            const WasmNoteEvent* moduleNotes = nullptr;
            int moduleNoteCount = 0;
            if (step.hasNoteCable)
            {
               static WasmNoteEvent mergedNotes[kMaxNoteEventsPerBlock];
               int mergedCount = 0;
               for (int scratchSlot : step.noteSourceScratchSlots)
               {
                  const int count = mNoteSourceCounts[static_cast<size_t>(scratchSlot)];
                  if (count <= 0)
                     continue;
                  const WasmNoteEvent* sourceNotes = mNoteSourceScratch[static_cast<size_t>(scratchSlot)].data();
                  for (int noteIndex = 0; noteIndex < count && mergedCount < kMaxNoteEventsPerBlock; ++noteIndex)
                     mergedNotes[mergedCount++] = sourceNotes[noteIndex];
               }
               if (mergedCount > 0)
               {
                  moduleNotes = mergedNotes;
                  moduleNoteCount = mergedCount;
               }
            }
            else if (globalMidiNotes)
            {
               moduleNotes = globalMidiNotes;
               moduleNoteCount = mMidiNoteCount;
            }

            WasmAudioProcessContext context;
            context.sampleRate = sampleRate;
            context.numSamples = numSamples;
            context.blockStartTimeSeconds = blockStart;
            if (moduleNotes && moduleNoteCount > 0)
            {
               context.notes = moduleNotes;
               context.noteCount = moduleNoteCount;
            }

            switch (step.processor)
            {
               case PlanProcessorKind::Noise:
               {
                  auto& state = stateFor(step.moduleId);
                  kNoiseAdapter.processAudio(&state.noiseState, node, buffer, context);
                  break;
               }
               case PlanProcessorKind::Oscillator:
               {
                  auto& state = stateFor(step.moduleId);
                  updateOscillatorType(state, node.waveform);
                  if (moduleNotes && moduleNoteCount > 0)
                  {
                     for (int noteIndex = 0; noteIndex < moduleNoteCount; ++noteIndex)
                     {
                        const auto& note = moduleNotes[noteIndex];
                        if (note.isNoteOn)
                        {
                           state.noteFrequency = 440.0f * std::pow(2.0f, (note.pitch - 69) / 12.0f);
                           state.noteVelocity = clampFloat(note.velocity, 0.0f, 1.0f);
                           state.noteGate = true;
                           state.hasReceivedNote = true;
                        }
                        else
                        {
                           state.noteGate = false;
                           state.hasReceivedNote = true;
                        }
                     }
                  }

                  const float frequency = state.hasReceivedNote ? state.noteFrequency : node.frequency;
                  float level = 1.0f;
                  if (step.hasNoteCable)
                  {
                     if (!state.hasReceivedNote || !state.noteGate)
                        level = 0.0f;
                     else
                        level = state.noteVelocity;
                  }
                  else if (state.hasReceivedNote)
                  {
                     level = state.noteGate ? state.noteVelocity : 0.0f;
                  }

                  const float volume = clampFloat(node.volume, 0.0f, 1.0f);
                  for (int i = 0; i < numSamples; ++i)
                     buffer[i] = renderOscillatorSample(state, frequency, sampleRate) * volume * level;
                  break;
               }
               case PlanProcessorKind::Filter:
               {
                  auto& state = stateFor(step.moduleId);
                  if (step.modulationSourceSlots.count > 0)
                  {
                     std::memset(mSummedModulation.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
                     for (int modSlot : step.modulationSourceSlots)
                     {
                        const float* modSource = modulationSlotBuffer(modSlot, numSamples);
                        if (!modSource)
                           continue;
                        for (int i = 0; i < numSamples; ++i)
                           mSummedModulation[static_cast<size_t>(i)] += modSource[i];
                     }
                     context.modulationBuffer = mSummedModulation.data();
                  }
                  kFilterAdapter.processAudio(&state.filterState, node, buffer, context);
                  break;
               }
               case PlanProcessorKind::Adsr:
               {
                  auto& state = stateFor(step.moduleId);
                  kAdsrAdapter.processAudio(&state.adsrState, node, buffer, context);
                  break;
               }
               case PlanProcessorKind::Delay:
               {
                  auto& state = stateFor(step.moduleId);
                  kDelayAdapter.processAudio(&state.delayState, node, buffer, context);
                  break;
               }
               case PlanProcessorKind::Gain:
               {
                  const float gain = clampFloat(node.gain, 0.0f, 1.0f);
                  for (int i = 0; i < numSamples; ++i)
                     buffer[i] *= gain;
                  break;
               }
               case PlanProcessorKind::Output:
               default:
                  break;
            }
         }

         const float* outBuffer = audioSlotBuffer(plan.outputBufferSlot, numSamples);
         if (!outBuffer)
            return;

         for (int ch = 0; ch < channels; ++ch)
         {
            if (!output[ch])
               continue;
            for (int i = 0; i < numSamples; ++i)
               output[ch][i] = clampFloat(outBuffer[i], -1.0f, 1.0f);
         }
         mAudioTimeSeconds += static_cast<double>(numSamples) / sampleRate;
      }

   } // namespace wasm
} // namespace bespoke
