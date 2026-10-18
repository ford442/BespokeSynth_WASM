/**
 * BespokeSynth WASM - Audio graph processor
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/InputAudioBus.h"
#include "BespokeWasm/ScaleQuantize.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }
      }

      struct AudioGraphEngine::ProcessScratch
      {
         std::array<NoteMessage, kMaxNoteRingCapacity> noteRing{};
         std::array<WasmNoteEvent, kMaxNoteEventsPerBlock> midiNoteScratch{};
         std::array<std::array<WasmNoteEvent, kMaxNoteEventsPerBlock>, kMaxEventSources> noteSourceScratch{};
         std::array<int, kMaxEventSources> noteSourceCounts{};
         std::array<std::array<WasmPulseEvent, kMaxNoteEventsPerBlock>, kMaxEventSources> pulseSourceScratch{};
         std::array<int, kMaxEventSources> pulseSourceCounts{};
         std::array<WasmNoteEvent, kMaxNoteEventsPerBlock> mergedNotes{};
         std::array<WasmPulseEvent, kMaxNoteEventsPerBlock> mergedPulses{};
      };

      AudioGraphEngine::AudioGraphEngine()
      : mScratch(std::make_unique<ProcessScratch>())
      {
      }

      AudioGraphEngine::~AudioGraphEngine()
      {
         for (auto& slot : mRuntimeSlots)
            destroySlot(slot);
      }

      void AudioGraphEngine::prepareForBlock(int maxBlockSize, int maxAudioSlots, int maxModulationSlots)
      {
         maxBlockSize = std::max(1, std::min(maxBlockSize, kMaxBlockSize));
         maxAudioSlots = std::max(1, maxAudioSlots);
         maxModulationSlots = std::max(0, maxModulationSlots);

         mPreparedBlockSize = maxBlockSize;
         mPreparedAudioSlots = maxAudioSlots;
         mPreparedModulationSlots = maxModulationSlots;

         const size_t audioSamples = static_cast<size_t>(maxAudioSlots) * 2u * static_cast<size_t>(maxBlockSize);
         if (mAudioArena.size() < audioSamples)
            mAudioArena.resize(audioSamples);
         if (mSlotChannels.size() < static_cast<size_t>(maxAudioSlots))
            mSlotChannels.resize(static_cast<size_t>(maxAudioSlots), 1);

         const size_t modSamples = static_cast<size_t>(maxModulationSlots) * static_cast<size_t>(maxBlockSize);
         if (mModulationArena.size() < modSamples)
            mModulationArena.resize(modSamples);

         if (mSummedModulation.size() < static_cast<size_t>(maxBlockSize))
            mSummedModulation.resize(static_cast<size_t>(maxBlockSize));
         if (mInputScratch.size() < static_cast<size_t>(maxBlockSize))
            mInputScratch.resize(static_cast<size_t>(maxBlockSize));
      }

      void AudioGraphEngine::resetClock()
      {
         mAudioTimeSeconds = 0.0;
         mBeatPosition = 0.0;
         mPublishedBeat.store(0.0, std::memory_order_relaxed);
         mNoteWriteIndex.store(0, std::memory_order_relaxed);
         mNoteReadIndex.store(0, std::memory_order_relaxed);
         mMidiNoteCount = 0;
         for (auto& slot : mRuntimeSlots)
            destroySlot(slot);
         mRuntimeSlots.clear();
         mSlotIndexByModuleId.clear();
         mRuntimeArena.clear();
      }

      void AudioGraphEngine::prepareRuntimeStates(const AudioGraphSnapshot& graph)
      {
         auto& registry = WasmModuleAdapterRegistry::instance();
         for (auto& slot : mRuntimeSlots)
         {
            if (slot.size == 0 || slot.dormant)
               continue;
            const bool present = std::any_of(graph.nodes.begin(), graph.nodes.end(),
                                             [&slot](const AudioGraphNode& node)
                                             {
                                                return node.id == slot.moduleId && node.typeIndex == slot.typeIndex;
                                             });
            if (present)
               continue;
            // Keep the arena bytes so an undo that restores this id reuses them.
            const WasmModuleAdapter* adapter = registry.adapterAt(slot.typeIndex);
            if (adapter && slot.offset + slot.size <= mRuntimeArena.size())
            {
               adapter->destroyRuntimeState(mRuntimeArena.data() + slot.offset);
               adapter->initRuntimeState(mRuntimeArena.data() + slot.offset);
            }
            slot.dormant = true;
         }

         for (const auto& node : graph.nodes)
         {
            stateFor(node);
            auto it = mSlotIndexByModuleId.find(node.id);
            if (it != mSlotIndexByModuleId.end())
               mRuntimeSlots[static_cast<size_t>(it->second)].dormant = false;
         }
      }

      void AudioGraphEngine::destroySlot(RuntimeSlot& slot)
      {
         if (slot.size == 0)
            return;
         const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().adapterAt(slot.typeIndex);
         if (adapter && slot.offset + slot.size <= mRuntimeArena.size())
            adapter->destroyRuntimeState(mRuntimeArena.data() + slot.offset);
         slot.size = 0;
      }

      void* AudioGraphEngine::stateFor(const AudioGraphNode& node)
      {
         const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().adapterAt(node.typeIndex);
         if (!adapter)
            return nullptr;

         const size_t needed = adapter->runtimeStateSize();
         if (needed == 0)
            return nullptr;

         auto it = mSlotIndexByModuleId.find(node.id);
         if (it != mSlotIndexByModuleId.end())
         {
            RuntimeSlot& slot = mRuntimeSlots[static_cast<size_t>(it->second)];
            if (slot.typeIndex == node.typeIndex && slot.size == needed)
               return mRuntimeArena.data() + slot.offset;
            destroySlot(slot);
         }

         const uint32_t offset = alignAudioGraphOffset(static_cast<uint32_t>(mRuntimeArena.size()), 16);
         mRuntimeArena.resize(static_cast<size_t>(offset) + needed, 0);

         RuntimeSlot slot;
         slot.moduleId = node.id;
         slot.typeIndex = node.typeIndex;
         slot.offset = offset;
         slot.size = static_cast<uint32_t>(needed);
         adapter->initRuntimeState(mRuntimeArena.data() + offset);

         if (it != mSlotIndexByModuleId.end())
         {
            mRuntimeSlots[static_cast<size_t>(it->second)] = slot;
         }
         else
         {
            mSlotIndexByModuleId[node.id] = static_cast<int>(mRuntimeSlots.size());
            mRuntimeSlots.push_back(slot);
         }
         return mRuntimeArena.data() + offset;
      }

      void AudioGraphEngine::resetEventSourceState()
      {
         auto& registry = WasmModuleAdapterRegistry::instance();
         for (auto& slot : mRuntimeSlots)
         {
            const WasmModuleAdapter* adapter = registry.adapterAt(slot.typeIndex);
            if (!adapter || slot.size == 0)
               continue;
            const WasmAudioRole role = adapter->audioRole();
            if (role != WasmAudioRole::NoteSource && role != WasmAudioRole::PulseSource)
               continue;
            adapter->destroyRuntimeState(mRuntimeArena.data() + slot.offset);
            adapter->initRuntimeState(mRuntimeArena.data() + slot.offset);
         }
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
         mScratch->noteRing[write] = note;
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

            const NoteMessage& note = mScratch->noteRing[read];
            mScratch->midiNoteScratch[static_cast<size_t>(mMidiNoteCount++)] = {
               note.pitch, note.velocity, note.isNoteOn, note.channel
            };
            mNoteReadIndex.store((read + 1) % kMaxNoteRingCapacity, std::memory_order_release);
         }
      }

      float* AudioGraphEngine::audioSlotBuffer(int slot, int channel, int numSamples)
      {
         if (slot < 0 || slot >= mPreparedAudioSlots || numSamples > mPreparedBlockSize || channel < 0 || channel > 1)
            return nullptr;
         const size_t block = static_cast<size_t>(mPreparedBlockSize);
         return mAudioArena.data() + (static_cast<size_t>(slot) * 2u + static_cast<size_t>(channel)) * block;
      }

      int AudioGraphEngine::audioSlotChannels(int slot) const
      {
         if (slot < 0 || static_cast<size_t>(slot) >= mSlotChannels.size())
            return 1;
         return mSlotChannels[static_cast<size_t>(slot)];
      }

      void AudioGraphEngine::publishOutputPeaks(const float* const* channels, int numChannels, int numSamples)
      {
         for (int ch = 0; ch < 2; ++ch)
         {
            const float peak = dsp::peakAbs(channels[std::min(ch, numChannels - 1)], numSamples);
            float held = mOutputPeak[ch].load(std::memory_order_relaxed);
            while (peak > held && !mOutputPeak[ch].compare_exchange_weak(held, peak, std::memory_order_relaxed))
            {
            }
         }
      }

      float AudioGraphEngine::takeOutputPeak(int channel)
      {
         if (channel < 0 || channel > 1)
            return 0.0f;
         return mOutputPeak[channel].exchange(0.0f, std::memory_order_relaxed);
      }

      float* AudioGraphEngine::modulationSlotBuffer(int slot, int numSamples)
      {
         if (slot < 0 || slot >= mPreparedModulationSlots || numSamples > mPreparedBlockSize)
            return nullptr;
         return mModulationArena.data() + static_cast<size_t>(slot) * static_cast<size_t>(mPreparedBlockSize);
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
               mPublishedBeat.store(0.0, std::memory_order_relaxed);
               resetEventSourceState();
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
         if (static_cast<int>(mInputScratch.size()) < numSamples)
            return;
         InputAudioBus::instance().consume(mInputScratch.data(), numSamples);

         const double beatStart = mBeatPosition;
         const double beatsPerBlock = static_cast<double>(numSamples) * graph.transportBPM /
                                      (static_cast<double>(sampleRate) * 60.0);
         const double beatEnd = beatStart + beatsPerBlock;
         mBeatPosition = beatEnd;
         mPublishedBeat.store(beatEnd, std::memory_order_relaxed);

         auto& registry = WasmModuleAdapterRegistry::instance();

         auto quantizeNotes = [&graph](WasmNoteEvent* notes, int count)
         {
            if (!notes || count <= 0)
               return;
            for (int i = 0; i < count; ++i)
               notes[i].pitch = quantizeMidiPitch(notes[i].pitch, graph.musicScale);
         };

         auto& scratch = *mScratch;
         scratch.pulseSourceCounts.fill(0);
         const int pulseSourceCount = std::min(static_cast<int>(plan.pulseSourceNodeIndices.size()), kMaxEventSources);
         for (int pulseSlot = 0; pulseSlot < pulseSourceCount; ++pulseSlot)
         {
            const int pulseNodeIndex = plan.pulseSourceNodeIndices[static_cast<size_t>(pulseSlot)];
            if (pulseNodeIndex < 0 || static_cast<size_t>(pulseNodeIndex) >= graph.nodes.size())
               continue;
            const auto& node = graph.nodes[static_cast<size_t>(pulseNodeIndex)];
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            if (!adapter)
               continue;
            const WasmPulseEvent* upstreamPulses = nullptr;
            int upstreamPulseCount = 0;
            if (pulseSlot < static_cast<int>(plan.pulseSourcePlans.size()))
            {
               const EventSourcePlan& sourcePlan = plan.pulseSourcePlans[static_cast<size_t>(pulseSlot)];
               int mergedCount = 0;
               for (int scratchSlot : sourcePlan.pulseSourceScratchSlots)
               {
                  // Plan order guarantees upstream slots were filled earlier this block.
                  if (scratchSlot < 0 || scratchSlot >= pulseSlot)
                     continue;
                  const int count = scratch.pulseSourceCounts[static_cast<size_t>(scratchSlot)];
                  const WasmPulseEvent* pulses = scratch.pulseSourceScratch[static_cast<size_t>(scratchSlot)].data();
                  for (int i = 0; i < count && mergedCount < kMaxNoteEventsPerBlock; ++i)
                     scratch.mergedPulses[static_cast<size_t>(mergedCount++)] = pulses[i];
               }
               if (mergedCount > 0)
               {
                  upstreamPulses = scratch.mergedPulses.data();
                  upstreamPulseCount = mergedCount;
               }
            }

            int& pulseCount = scratch.pulseSourceCounts[static_cast<size_t>(pulseSlot)];
            pulseCount = 0;
            adapter->emitPulsesForBeatRange(
            stateFor(node),
            graph.paramsFor(node),
            beatStart,
            beatEnd,
            scratch.pulseSourceScratch[static_cast<size_t>(pulseSlot)].data(),
            kMaxNoteEventsPerBlock,
            pulseCount,
            upstreamPulses,
            upstreamPulseCount);
         }

         scratch.noteSourceCounts.fill(0);
         const int noteSourceCount = std::min(static_cast<int>(plan.noteSourcePlans.size()), kMaxEventSources);
         for (int seqSlot = 0; seqSlot < noteSourceCount; ++seqSlot)
         {
            const EventSourcePlan& sourcePlan = plan.noteSourcePlans[static_cast<size_t>(seqSlot)];
            if (sourcePlan.nodeIndex < 0 || static_cast<size_t>(sourcePlan.nodeIndex) >= graph.nodes.size())
               continue;
            const auto& node = graph.nodes[static_cast<size_t>(sourcePlan.nodeIndex)];
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            if (!adapter)
               continue;

            const WasmPulseEvent* sourcePulses = nullptr;
            int sourcePulseCount = 0;
            if (sourcePlan.hasPulseCable)
            {
               int mergedCount = 0;
               for (int scratchSlot : sourcePlan.pulseSourceScratchSlots)
               {
                  if (scratchSlot < 0 || scratchSlot >= kMaxEventSources)
                     continue;
                  const int count = scratch.pulseSourceCounts[static_cast<size_t>(scratchSlot)];
                  if (count <= 0)
                     continue;
                  const WasmPulseEvent* pulses = scratch.pulseSourceScratch[static_cast<size_t>(scratchSlot)].data();
                  for (int i = 0; i < count && mergedCount < kMaxNoteEventsPerBlock; ++i)
                     scratch.mergedPulses[static_cast<size_t>(mergedCount++)] = pulses[i];
               }
               if (mergedCount > 0)
               {
                  sourcePulses = scratch.mergedPulses.data();
                  sourcePulseCount = mergedCount;
               }
            }

            int& noteCount = scratch.noteSourceCounts[static_cast<size_t>(seqSlot)];
            noteCount = 0;
            adapter->emitNotesForBeatRange(
            stateFor(node),
            graph.paramsFor(node),
            beatStart,
            beatEnd,
            scratch.noteSourceScratch[static_cast<size_t>(seqSlot)].data(),
            kMaxNoteEventsPerBlock,
            noteCount,
            sourcePulses,
            sourcePulseCount);
            if (adapter->quantizesNotesToScale())
               quantizeNotes(scratch.noteSourceScratch[static_cast<size_t>(seqSlot)].data(), noteCount);
         }

         quantizeNotes(scratch.midiNoteScratch.data(), mMidiNoteCount);

         WasmAudioProcessContext modulationContext;
         modulationContext.sampleRate = sampleRate;
         modulationContext.numSamples = numSamples;
         modulationContext.blockStartTimeSeconds = mAudioTimeSeconds;
         modulationContext.beatStart = beatStart;
         modulationContext.beatEnd = beatEnd;
         modulationContext.bpm = graph.transportBPM;
         modulationContext.inputAudio = mInputScratch.data();

         for (int lfoSlot = 0; lfoSlot < static_cast<int>(plan.modulationSourceNodeIndices.size()); ++lfoSlot)
         {
            const int lfoNodeIndex = plan.modulationSourceNodeIndices[static_cast<size_t>(lfoSlot)];
            const auto& node = graph.nodes[static_cast<size_t>(lfoNodeIndex)];
            float* buffer = modulationSlotBuffer(lfoSlot, numSamples);
            if (!buffer)
               continue;
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            if (!adapter)
               continue;
            adapter->processMono(stateFor(node), graph.paramsFor(node), buffer, modulationContext);
         }

         const double blockStart = mAudioTimeSeconds;
         const WasmNoteEvent* globalMidiNotes =
         mMidiNoteCount > 0 ? scratch.midiNoteScratch.data() : nullptr;

         for (const auto& step : plan.steps)
         {
            if (step.nodeIndex < 0 || static_cast<size_t>(step.nodeIndex) >= graph.nodes.size())
               continue;
            const auto& node = graph.nodes[static_cast<size_t>(step.nodeIndex)];
            float* channels[2] = { audioSlotBuffer(step.outBufferSlot, 0, numSamples),
                                   audioSlotBuffer(step.outBufferSlot, 1, numSamples) };
            if (!channels[0])
               continue;

            const WasmModuleAdapter* stepAdapter = registry.adapterAt(step.typeIndex);
            const bool sumInputs = !stepAdapter || stepAdapter->sumsAudioInputs();
            std::array<WasmAudioInput, decltype(step.inputBufferSlots)::kCapacity> audioInputs{};
            int audioInputCount = 0;
            int inputChannels = 1;
            for (int inputIndex = 0; inputIndex < step.inputBufferSlots.size(); ++inputIndex)
            {
               const int inputSlot = step.inputBufferSlots.data[inputIndex];
               const float* left = audioSlotBuffer(inputSlot, 0, numSamples);
               if (!left)
                  continue;
               // Mono upmix is free: a mono slot hands out its L buffer as R.
               WasmAudioInput& input = audioInputs[static_cast<size_t>(audioInputCount++)];
               input.numChannels = audioSlotChannels(inputSlot);
               input.channels[0] = left;
               input.channels[1] = input.numChannels == 2 ? audioSlotBuffer(inputSlot, 1, numSamples) : left;
               input.port = step.inputBufferPorts.data[inputIndex];
               inputChannels = std::max(inputChannels, input.numChannels);
            }

            const WasmNoteEvent* moduleNotes = nullptr;
            int moduleNoteCount = 0;
            if (step.hasNoteCable)
            {
               int mergedCount = 0;
               for (int cable = 0; cable < step.noteSourceScratchSlots.size(); ++cable)
               {
                  const int scratchSlot = step.noteSourceScratchSlots.data[cable];
                  const int portFilter = step.noteSourcePortFilters.data[cable];
                  if (scratchSlot < 0 || scratchSlot >= kMaxEventSources)
                     continue;
                  const int count = scratch.noteSourceCounts[static_cast<size_t>(scratchSlot)];
                  if (count <= 0)
                     continue;
                  const WasmNoteEvent* sourceNotes = scratch.noteSourceScratch[static_cast<size_t>(scratchSlot)].data();
                  for (int noteIndex = 0; noteIndex < count && mergedCount < kMaxNoteEventsPerBlock; ++noteIndex)
                  {
                     if (portFilter >= 0 && sourceNotes[noteIndex].outputPort != portFilter)
                        continue;
                     scratch.mergedNotes[static_cast<size_t>(mergedCount++)] = sourceNotes[noteIndex];
                  }
               }
               if (mergedCount > 0)
               {
                  moduleNotes = scratch.mergedNotes.data();
                  moduleNoteCount = mergedCount;
               }
            }
            else if (globalMidiNotes)
            {
               moduleNotes = globalMidiNotes;
               moduleNoteCount = mMidiNoteCount;
            }

            const WasmPulseEvent* modulePulses = nullptr;
            int modulePulseCount = 0;
            if (step.hasPulseCable)
            {
               int mergedCount = 0;
               for (int scratchSlot : step.pulseSourceScratchSlots)
               {
                  if (scratchSlot < 0 || scratchSlot >= kMaxEventSources)
                     continue;
                  const int count = scratch.pulseSourceCounts[static_cast<size_t>(scratchSlot)];
                  if (count <= 0)
                     continue;
                  const WasmPulseEvent* sourcePulses = scratch.pulseSourceScratch[static_cast<size_t>(scratchSlot)].data();
                  for (int i = 0; i < count && mergedCount < kMaxNoteEventsPerBlock; ++i)
                     scratch.mergedPulses[static_cast<size_t>(mergedCount++)] = sourcePulses[i];
               }
               if (mergedCount > 0)
               {
                  modulePulses = scratch.mergedPulses.data();
                  modulePulseCount = mergedCount;
               }
            }

            WasmAudioProcessContext context;
            context.sampleRate = sampleRate;
            context.numSamples = numSamples;
            context.blockStartTimeSeconds = blockStart;
            context.beatStart = beatStart;
            context.beatEnd = beatEnd;
            context.bpm = graph.transportBPM;
            context.inputAudio = mInputScratch.data();
            context.noteFrequencies = graph.musicScale.noteFrequency.data();
            context.hasNoteCable = step.hasNoteCable;
            context.hasPulseCable = step.hasPulseCable;
            context.hasAudioInput = step.inputBufferSlots.count > 0;
            context.audioInputs = audioInputCount > 0 ? audioInputs.data() : nullptr;
            context.audioInputCount = audioInputCount;
            context.inputChannels = inputChannels;

            const void* params = graph.paramsFor(node);
            const int numChannels = std::max(1, std::min(2, stepAdapter ? stepAdapter->outputChannelCount(params, context)
                                                                        : inputChannels));
            context.numChannels = numChannels;
            if (numChannels < 2)
               channels[1] = nullptr;
            for (int ch = 0; ch < numChannels; ++ch)
               std::memset(channels[ch], 0, static_cast<size_t>(numSamples) * sizeof(float));
            if (sumInputs)
            {
               for (int c = 0; c < audioInputCount; ++c)
                  accumulateAudioInput(audioInputs[static_cast<size_t>(c)], channels, numChannels, numSamples);
            }
            mSlotChannels[static_cast<size_t>(step.outBufferSlot)] = static_cast<uint8_t>(numChannels);
            if (step.audioRole == WasmAudioRole::Sink)
               publishOutputPeaks(channels, numChannels, numSamples);
            if (moduleNotes && moduleNoteCount > 0)
            {
               context.notes = moduleNotes;
               context.noteCount = moduleNoteCount;
            }
            if (modulePulses && modulePulseCount > 0)
            {
               context.pulses = modulePulses;
               context.pulseCount = modulePulseCount;
            }

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

            if (stepAdapter)
               stepAdapter->processAudio(stateFor(node), params, channels, context);
         }

         const int outChannels = audioSlotChannels(plan.outputBufferSlot);
         const float* outBuffers[2] = { audioSlotBuffer(plan.outputBufferSlot, 0, numSamples),
                                        audioSlotBuffer(plan.outputBufferSlot, outChannels == 2 ? 1 : 0, numSamples) };
         if (!outBuffers[0])
            return;

         // The Output limiter normally makes this clamp a no-op; it remains the last guard when the limiter is off.
         if (channels == 1 && outChannels == 2 && output[0])
         {
            for (int i = 0; i < numSamples; ++i)
               output[0][i] = clampFloat((outBuffers[0][i] + outBuffers[1][i]) * 0.5f, -1.0f, 1.0f);
         }
         else
         {
            for (int ch = 0; ch < channels; ++ch)
            {
               if (!output[ch])
                  continue;
               for (int i = 0; i < numSamples; ++i)
                  output[ch][i] = clampFloat(outBuffers[ch][i], -1.0f, 1.0f);
            }
         }
         mAudioTimeSeconds += static_cast<double>(numSamples) / sampleRate;
      }

   } // namespace wasm
} // namespace bespoke
