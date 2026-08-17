/**
 * BespokeSynth WASM - Audio graph processor
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioGraphEngine.h"
#include <algorithm>
#include <cstring>

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

         const size_t audioSamples = static_cast<size_t>(maxAudioSlots) * static_cast<size_t>(maxBlockSize);
         if (mAudioArena.size() < audioSamples)
            mAudioArena.resize(audioSamples);

         const size_t modSamples = static_cast<size_t>(maxModulationSlots) * static_cast<size_t>(maxBlockSize);
         if (mModulationArena.size() < modSamples)
            mModulationArena.resize(modSamples);

         if (mSummedModulation.size() < static_cast<size_t>(maxBlockSize))
            mSummedModulation.resize(static_cast<size_t>(maxBlockSize));
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

      void AudioGraphEngine::resetNoteSourceState()
      {
         auto& registry = WasmModuleAdapterRegistry::instance();
         for (auto& slot : mRuntimeSlots)
         {
            const WasmModuleAdapter* adapter = registry.adapterAt(slot.typeIndex);
            if (!adapter || adapter->audioRole() != WasmAudioRole::NoteSource || slot.size == 0)
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
               resetNoteSourceState();
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

         auto& registry = WasmModuleAdapterRegistry::instance();

         mNoteSourceCounts.fill(0);
         for (int seqSlot = 0; seqSlot < static_cast<int>(plan.noteSourceNodeIndices.size()); ++seqSlot)
         {
            const int seqNodeIndex = plan.noteSourceNodeIndices[static_cast<size_t>(seqSlot)];
            const auto& node = graph.nodes[static_cast<size_t>(seqNodeIndex)];
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            if (!adapter)
               continue;
            int& noteCount = mNoteSourceCounts[static_cast<size_t>(seqSlot)];
            noteCount = 0;
            adapter->emitNotesForBeatRange(
               stateFor(node),
               graph.paramsFor(node),
               beatStart,
               beatEnd,
               mNoteSourceScratch[static_cast<size_t>(seqSlot)].data(),
               kMaxNoteEventsPerBlock,
               noteCount);
         }

         WasmAudioProcessContext modulationContext;
         modulationContext.sampleRate = sampleRate;
         modulationContext.numSamples = numSamples;
         modulationContext.blockStartTimeSeconds = mAudioTimeSeconds;

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
            adapter->processAudio(stateFor(node), graph.paramsFor(node), buffer, modulationContext);
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
            context.hasNoteCable = step.hasNoteCable;
            if (moduleNotes && moduleNoteCount > 0)
            {
               context.notes = moduleNotes;
               context.noteCount = moduleNoteCount;
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

            if (const WasmModuleAdapter* adapter = registry.adapterAt(step.typeIndex))
               adapter->processAudio(stateFor(node), graph.paramsFor(node), buffer, context);
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
