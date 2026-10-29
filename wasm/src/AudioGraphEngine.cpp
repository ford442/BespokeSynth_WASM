/**
 * BespokeSynth WASM - Audio graph processor
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/AudioRtGuard.h"
#include "BespokeWasm/InputAudioBus.h"
#include "BespokeWasm/ScaleQuantize.h"
#include "BespokeWasm/ScriptNoteQueue.h"
#include <algorithm>
#include <array>
#include <cmath>
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

         /** a was queued after b, with wrap-around (serial number arithmetic). */
         bool seqAfter(uint32_t a, uint32_t b)
         {
            return static_cast<int32_t>(a - b) > 0;
         }

         template <typename T>
         void growTo(std::vector<T>& v, size_t size)
         {
            if (v.size() < size)
               v.resize(size);
         }
      }

      struct AudioGraphEngine::ProcessScratch
      {
         std::array<NoteMessage, kMaxNoteRingCapacity> noteRing{};
         /** Drained from the ring, not yet due (timed) or over the per-block cap; queue order. */
         std::array<NoteMessage, kMaxPendingNotes> pending{};
         /** Notes due in the current block, sorted by offset (queue order among equal offsets). */
         std::array<BlockNote, kMaxNoteEventsPerBlock> blockNotes{};
         /** Performance notes of the piece being rendered. */
         std::array<WasmNoteEvent, kMaxNoteEventsPerBlock> midiNoteScratch{};
         std::array<WasmNoteEvent, kMaxNoteEventsPerBlock> mergedNotes{};
         std::array<WasmPulseEvent, kMaxNoteEventsPerBlock> mergedPulses{};
         std::array<MidiOutNote, kMidiOutRingCapacity> midiOutRing{};
         /** Bit c of [pitch]: a note-on reached the MIDI-out ring on channel c without its note-off. */
         std::array<uint16_t, 128> midiOutHeld{};
         /** Stopped transport, per key: bit c of [pitch] = touched / still held on channel c. */
         std::array<uint16_t, 128> stoppedTouched{};
         std::array<uint16_t, 128> stoppedHeld{};
         std::array<float, 128 * 16> stoppedVelocity{};
         std::array<ParamChange, kParamQueueCapacity> paramQueue{};
         std::array<ParamChange, kRecentParamCapacity> recentParams{};
         std::array<ParamSmoother, kMaxParamSmoothers> smoothers{};
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
         mPendingCount = 0;
         mBlockNoteCount = 0;
         // The MIDI-out ring is not cleared: it carries the note-offs for whatever is still sounding
         // on the external port, which the host drains after the reset.
         flushMidiOutHeld();
         mScratch->midiOutHeld.fill(0);
         mScratch->stoppedTouched.fill(0);
         mScratch->stoppedHeld.fill(0);
         mStoppedTouchedCount = 0;
         mTransportWasPlaying = false;
         mMidiNoteCount = 0;
         for (auto& slot : mRuntimeSlots)
            destroySlot(slot);
         mRuntimeSlots.clear();
         mSlotIndexByModuleId.clear();
         mRuntimeArena.clear();
         mNodeRuntime.clear();
         // The next block re-adopts its snapshot; queued changes stay (they are newer than it).
         mAdoptedGraph = nullptr;
         mLiveParamsValid = false;
         mRecentParamCount = 0;
         for (auto& smoother : mScratch->smoothers)
            smoother.active = false;
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

         mNodeRuntime.assign(graph.nodes.size(), NodeRuntime{});
         for (size_t i = 0; i < graph.nodes.size(); ++i)
         {
            const auto& node = graph.nodes[i];
            stateFor(node);
            NodeRuntime& runtime = mNodeRuntime[i];
            runtime.moduleId = node.id;
            runtime.typeIndex = node.typeIndex;
            auto it = mSlotIndexByModuleId.find(node.id);
            if (it != mSlotIndexByModuleId.end())
            {
               RuntimeSlot& slot = mRuntimeSlots[static_cast<size_t>(it->second)];
               slot.dormant = false;
               if (slot.size > 0 && slot.typeIndex == node.typeIndex)
                  runtime.slotIndex = it->second;
            }
         }

         const AudioProcessPlan& plan = graph.processPlan;
         const size_t perSource = static_cast<size_t>(kMaxNoteEventsPerBlock);
         growTo(mNoteSourceScratch, plan.noteSourcePlans.size() * perSource);
         growTo(mNoteSourceCounts, plan.noteSourcePlans.size());
         growTo(mPulseSourceScratch, plan.pulseSourceNodeIndices.size() * perSource);
         growTo(mPulseSourceCounts, plan.pulseSourceNodeIndices.size());
         growTo(mAudioInputScratch, static_cast<size_t>(plan.maxAudioFanIn));
         growTo(mDownmixScratch, static_cast<size_t>(plan.maxAudioFanIn) * static_cast<size_t>(mPreparedBlockSize));
         // Adoption copies the arena with assign(), which then stays within capacity.
         if (mLiveParams.capacity() < graph.paramArena.size())
            mLiveParams.reserve(graph.paramArena.size() * 2);
         mPrepareCount.fetch_add(1, std::memory_order_relaxed);
      }

      bool AudioGraphEngine::preparedState(int nodeIndex, const AudioGraphNode& node, void*& state)
      {
         state = nullptr;
         const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().adapterAt(node.typeIndex);
         if (!adapter || adapter->runtimeStateSize() == 0)
            return true;
         int slotIndex = -1;
         if (nodeIndex >= 0 && static_cast<size_t>(nodeIndex) < mNodeRuntime.size())
         {
            const NodeRuntime& runtime = mNodeRuntime[static_cast<size_t>(nodeIndex)];
            if (runtime.moduleId == node.id && runtime.typeIndex == node.typeIndex)
               slotIndex = runtime.slotIndex;
         }
         if (slotIndex < 0)
         {
            // A snapshot other than the prepared one (same modules, other order): find() does not allocate.
            auto it = mSlotIndexByModuleId.find(node.id);
            if (it != mSlotIndexByModuleId.end())
               slotIndex = it->second;
         }
         if (slotIndex < 0)
            return false;
         const RuntimeSlot& slot = mRuntimeSlots[static_cast<size_t>(slotIndex)];
         if (slot.typeIndex != node.typeIndex || slot.size != adapter->runtimeStateSize() ||
             slot.offset + slot.size > mRuntimeArena.size())
            return false;
         state = mRuntimeArena.data() + slot.offset;
         return true;
      }

      bool AudioGraphEngine::queueParamChange(const ParamChange& change)
      {
         const uint32_t write = mParamWriteIndex.load(std::memory_order_relaxed);
         const uint32_t nextWrite = (write + 1) % kParamQueueCapacity;
         if (nextWrite == mParamReadIndex.load(std::memory_order_acquire))
         {
            mParamChangesDropped.fetch_add(1, std::memory_order_relaxed);
            return false;
         }
         mScratch->paramQueue[write] = change;
         mParamWriteIndex.store(nextWrite, std::memory_order_release);
         return true;
      }

      AudioGraphEngine::Stats AudioGraphEngine::stats() const
      {
         Stats stats;
         stats.prepareCount = mPrepareCount.load(std::memory_order_relaxed);
         stats.adoptCount = mAdoptCount.load(std::memory_order_relaxed);
         stats.paramChangesApplied = mParamChangesApplied.load(std::memory_order_relaxed);
         stats.paramChangesDropped = mParamChangesDropped.load(std::memory_order_relaxed);
         stats.paramChangesStale = mParamChangesStale.load(std::memory_order_relaxed);
         stats.skippedNodes = mSkippedNodes.load(std::memory_order_relaxed);
         stats.timedNotes = mTimedNotes.load(std::memory_order_relaxed);
         stats.lateNotes = mLateNotes.load(std::memory_order_relaxed);
         stats.splitBlocks = mSplitBlocks.load(std::memory_order_relaxed);
         return stats;
      }

      void AudioGraphEngine::applyQueuedParams(const AudioGraphSnapshot& graph)
      {
         adoptSnapshot(graph);
         drainParamQueue(graph);
      }

      const void* AudioGraphEngine::liveParamsFor(const AudioGraphSnapshot& graph, const AudioGraphNode& node) const
      {
         if (!mLiveParamsValid)
            return graph.paramsFor(node);
         if (node.paramSize == 0 || static_cast<size_t>(node.paramOffset) + node.paramSize > mLiveParams.size())
            return nullptr;
         return mLiveParams.data() + node.paramOffset;
      }

      void* AudioGraphEngine::writableLiveParamsFor(const AudioGraphSnapshot& graph, const AudioGraphNode& node)
      {
         if (!mLiveParamsValid)
            return nullptr; // untracked snapshot: params are read-only
         return const_cast<void*>(static_cast<const AudioGraphEngine*>(this)->liveParamsFor(graph, node));
      }

      void AudioGraphEngine::adoptSnapshot(const AudioGraphSnapshot& graph)
      {
         if (graph.generation == 0)
         {
            // Untracked (tests, offline): the snapshot is the truth every block.
            mAdoptedGraph = &graph;
            mAdoptedGeneration = 0;
            mLiveParamsValid = false;
            mLivePlaying = graph.transportPlaying;
            mLiveBpm = graph.transportBPM;
            return;
         }
         if (mAdoptedGraph == &graph && mAdoptedGeneration == graph.generation && mLiveParamsValid)
            return;

         mAdoptedGraph = &graph;
         mAdoptedGeneration = graph.generation;
         mAdoptedParamSeq = graph.paramSeq;
         mLivePlaying = graph.transportPlaying;
         mLiveBpm = graph.transportBPM;
         for (auto& smoother : mScratch->smoothers)
            smoother.active = false;
         mLiveParamsValid = graph.paramArena.size() <= mLiveParams.capacity();
         if (!mLiveParamsValid)
            return; // not prepared for this arena: read the snapshot, ignore changes until it is
         mLiveParams.assign(graph.paramArena.begin(), graph.paramArena.end());
         mAdoptCount.fetch_add(1, std::memory_order_relaxed);

         // Changes drained before this snapshot arrived but made after it was built.
         const int count = mRecentParamCount;
         const int first = (mRecentParamNext - count + kRecentParamCapacity) % kRecentParamCapacity;
         for (int i = 0; i < count; ++i)
         {
            const ParamChange& change = mScratch->recentParams[static_cast<size_t>((first + i) % kRecentParamCapacity)];
            if (seqAfter(change.seq, mAdoptedParamSeq))
               applyParamChange(graph, change, false);
         }
      }

      void AudioGraphEngine::drainParamQueue(const AudioGraphSnapshot& graph)
      {
         auto& scratch = *mScratch;
         while (true)
         {
            const uint32_t read = mParamReadIndex.load(std::memory_order_relaxed);
            if (read == mParamWriteIndex.load(std::memory_order_acquire))
               break;
            const ParamChange change = scratch.paramQueue[read];
            mParamReadIndex.store((read + 1) % kParamQueueCapacity, std::memory_order_release);

            if (mAdoptedGeneration != 0 && !seqAfter(change.seq, mAdoptedParamSeq))
            {
               mParamChangesStale.fetch_add(1, std::memory_order_relaxed);
               continue;
            }
            scratch.recentParams[static_cast<size_t>(mRecentParamNext)] = change;
            mRecentParamNext = (mRecentParamNext + 1) % kRecentParamCapacity;
            mRecentParamCount = std::min(mRecentParamCount + 1, kRecentParamCapacity);
            applyParamChange(graph, change, true);
         }
      }

      void AudioGraphEngine::applyParamChange(const AudioGraphSnapshot& graph, const ParamChange& change, bool smooth)
      {
         if (change.moduleId == kGlobalParamModule)
         {
            if (change.paramId == static_cast<ParamId>(GlobalParam::TransportPlaying))
               mLivePlaying = change.value >= 0.5f;
            else if (change.paramId == static_cast<ParamId>(GlobalParam::TransportBpm) && change.value > 0.0f)
               mLiveBpm = change.value;
            mParamChangesApplied.fetch_add(1, std::memory_order_relaxed);
            return;
         }
         const auto& byModule = graph.processPlan.nodeIndexByModuleId;
         if (change.moduleId < 0 || static_cast<size_t>(change.moduleId) >= byModule.size())
            return;
         const int nodeIndex = byModule[static_cast<size_t>(change.moduleId)];
         if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= graph.nodes.size())
            return;
         const AudioGraphNode& node = graph.nodes[static_cast<size_t>(nodeIndex)];
         const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().adapterAt(node.typeIndex);
         void* params = writableLiveParamsFor(graph, node);
         if (!adapter || !params)
            return;
         const ParamTable table = adapter->paramTable();
         if (change.paramId >= table.size())
            return;
         const ParamDescriptor& param = table[change.paramId];
         if (!param.isAudio() || param.offset + sizeof(float) > node.paramSize)
            return;
         mParamChangesApplied.fetch_add(1, std::memory_order_relaxed);

         if (smooth && param.kind == ParamKind::Float && param.smoothingMs > 0.0f)
         {
            float target = change.value;
            if ((param.flags & kParamClamp) != 0)
               target = clampFloat(target, param.minValue, param.maxValue);
            const uint32_t liveOffset = node.paramOffset + param.offset;
            ParamSmoother* freeSmoother = nullptr;
            for (auto& smoother : mScratch->smoothers)
            {
               if (smoother.active && smoother.liveOffset == liveOffset)
               {
                  smoother.target = target;
                  return;
               }
               if (!smoother.active && !freeSmoother)
                  freeSmoother = &smoother;
            }
            if (freeSmoother)
            {
               freeSmoother->liveOffset = liveOffset;
               freeSmoother->nodeIndex = nodeIndex;
               freeSmoother->current = readParam(params, param);
               freeSmoother->target = target;
               freeSmoother->smoothingMs = param.smoothingMs;
               freeSmoother->active = true;
               return;
            }
            // No smoother free: jump.
         }
         writeParam(params, param, change.value);
         adapter->finalizeParams(params);
      }

      void AudioGraphEngine::advanceSmoothers(const AudioGraphSnapshot& graph, int numSamples, float sampleRate)
      {
         if (!mLiveParamsValid)
            return;
         auto& registry = WasmModuleAdapterRegistry::instance();
         for (auto& smoother : mScratch->smoothers)
         {
            if (!smoother.active)
               continue;
            if (smoother.nodeIndex < 0 || static_cast<size_t>(smoother.nodeIndex) >= graph.nodes.size() ||
                smoother.liveOffset + sizeof(float) > mLiveParams.size())
            {
               smoother.active = false;
               continue;
            }
            const float blockMs = 1000.0f * static_cast<float>(numSamples) / sampleRate;
            const float coeff = 1.0f - std::exp(-blockMs / smoother.smoothingMs);
            smoother.current += (smoother.target - smoother.current) * coeff;
            const float span = std::max(std::fabs(smoother.target), 1.0f);
            if (std::fabs(smoother.target - smoother.current) <= span * 1.0e-4f)
            {
               smoother.current = smoother.target;
               smoother.active = false;
            }
            std::memcpy(mLiveParams.data() + smoother.liveOffset, &smoother.current, sizeof(float));
            const AudioGraphNode& node = graph.nodes[static_cast<size_t>(smoother.nodeIndex)];
            if (const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex))
               adapter->finalizeParams(mLiveParams.data() + node.paramOffset);
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
            if (role != WasmAudioRole::NoteSource && role != WasmAudioRole::NoteProcessor &&
                role != WasmAudioRole::PulseSource)
               continue;
            adapter->destroyRuntimeState(mRuntimeArena.data() + slot.offset);
            adapter->initRuntimeState(mRuntimeArena.data() + slot.offset);
         }
      }

      void AudioGraphEngine::queueNote(const NoteMessage& note)
      {
         const uint32_t write = mNoteWriteIndex.load(std::memory_order_relaxed);
         const uint32_t nextWrite = (write + 1) % kMaxNoteRingCapacity;
         if (nextWrite == mNoteReadIndex.load(std::memory_order_acquire))
         {
            mNoteDropCount.fetch_add(1, std::memory_order_relaxed);
            return;
         }
         mScratch->noteRing[write] = note;
         mNoteWriteIndex.store(nextWrite, std::memory_order_release);
      }

      bool AudioGraphEngine::popRingNote(NoteMessage& note)
      {
         const uint32_t read = mNoteReadIndex.load(std::memory_order_relaxed);
         if (read == mNoteWriteIndex.load(std::memory_order_acquire))
            return false;
         note = mScratch->noteRing[read];
         mNoteReadIndex.store((read + 1) % kMaxNoteRingCapacity, std::memory_order_release);
         return true;
      }

      void AudioGraphEngine::foldNoteWhileStopped(const NoteMessage& note)
      {
         if (note.pitch < 0 || note.pitch >= 128)
            return;
         if (note.scriptModuleId >= 0)
         {
            // A stopped Script module emits nothing; its note-offs still go out on Play so held voices let go.
            if (!note.isNoteOn)
               (void)pushScriptNote(note.scriptModuleId, { note.pitch, 0.0f, false, note.channel });
            return;
         }
         auto& scratch = *mScratch;
         const size_t pitch = static_cast<size_t>(note.pitch);
         const int channel = note.channel & 0x0f;
         const auto bit = static_cast<uint16_t>(1u << channel);
         if ((scratch.stoppedTouched[pitch] & bit) == 0)
            ++mStoppedTouchedCount;
         scratch.stoppedTouched[pitch] = static_cast<uint16_t>(scratch.stoppedTouched[pitch] | bit);
         if (note.isNoteOn)
         {
            scratch.stoppedHeld[pitch] = static_cast<uint16_t>(scratch.stoppedHeld[pitch] | bit);
            scratch.stoppedVelocity[pitch * 16u + static_cast<size_t>(channel)] = note.velocity;
         }
         else
         {
            scratch.stoppedHeld[pitch] = static_cast<uint16_t>(scratch.stoppedHeld[pitch] & ~bit);
         }
      }

      void AudioGraphEngine::absorbQueuedNotesWhileStopped()
      {
         // Pending notes were queued before anything still in the ring.
         for (int i = 0; i < mPendingCount; ++i)
            foldNoteWhileStopped(mScratch->pending[static_cast<size_t>(i)]);
         mPendingCount = 0;
         NoteMessage note;
         while (popRingNote(note))
            foldNoteWhileStopped(note);
      }

      bool AudioGraphEngine::emitStoppedNotes()
      {
         auto& scratch = *mScratch;
         mBlockNoteCount = 0;
         for (int pitch = 0; pitch < 128 && mStoppedTouchedCount > 0; ++pitch)
         {
            uint16_t& touched = scratch.stoppedTouched[static_cast<size_t>(pitch)];
            for (int channel = 0; channel < 16 && touched != 0; ++channel)
            {
               const auto bit = static_cast<uint16_t>(1u << channel);
               if ((touched & bit) == 0)
                  continue;
               if (mBlockNoteCount >= kMaxNoteEventsPerBlock)
                  return false;
               const bool held = (scratch.stoppedHeld[static_cast<size_t>(pitch)] & bit) != 0;
               BlockNote& out = scratch.blockNotes[static_cast<size_t>(mBlockNoteCount++)];
               out.offset = 0;
               out.note = NoteMessage{};
               out.note.pitch = pitch;
               out.note.channel = channel;
               out.note.isNoteOn = held;
               out.note.velocity =
               held ? scratch.stoppedVelocity[static_cast<size_t>(pitch) * 16u + static_cast<size_t>(channel)] : 0.0f;
               touched = static_cast<uint16_t>(touched & ~bit);
               --mStoppedTouchedCount;
            }
         }
         scratch.stoppedHeld.fill(0);
         return true;
      }

      int AudioGraphEngine::takeQueuedNotes(NoteMessage* dst, int maxNotes)
      {
         int count = 0;
         auto& scratch = *mScratch;
         for (int pitch = 0; pitch < 128 && dst && mStoppedTouchedCount > 0; ++pitch)
         {
            uint16_t& touched = scratch.stoppedTouched[static_cast<size_t>(pitch)];
            for (int channel = 0; channel < 16 && touched != 0 && count < maxNotes; ++channel)
            {
               const auto bit = static_cast<uint16_t>(1u << channel);
               if ((touched & bit) == 0)
                  continue;
               NoteMessage& note = dst[count++];
               note = NoteMessage{};
               note.pitch = pitch;
               note.channel = channel;
               note.isNoteOn = (scratch.stoppedHeld[static_cast<size_t>(pitch)] & bit) != 0;
               note.velocity =
               note.isNoteOn ? scratch.stoppedVelocity[static_cast<size_t>(pitch) * 16u + static_cast<size_t>(channel)]
                             : 0.0f;
               touched = static_cast<uint16_t>(touched & ~bit);
               --mStoppedTouchedCount;
            }
         }
         if (dst && mPendingCount > 0)
         {
            const int taken = std::min(mPendingCount, std::max(0, maxNotes - count));
            auto& pending = scratch.pending;
            std::copy(pending.begin(), pending.begin() + taken, dst + count);
            std::copy(pending.begin() + taken, pending.begin() + mPendingCount, pending.begin());
            mPendingCount -= taken;
            count += taken;
         }
         NoteMessage note;
         while (dst && count < maxNotes && popRingNote(note))
            dst[count++] = note;
         return count;
      }

      void AudioGraphEngine::collectBlockNotes(double hostTimeSeconds, int numSamples, float sampleRate)
      {
         auto& scratch = *mScratch;
         NoteMessage incoming;
         while (mPendingCount < kMaxPendingNotes && popRingNote(incoming))
            scratch.pending[static_cast<size_t>(mPendingCount++)] = incoming;

         const bool clocked = hostTimeSeconds >= 0.0 && sampleRate > 0.0f;
         const double horizonFrames = kMaxScheduleAheadSeconds * static_cast<double>(sampleRate);
         auto sameKey = [](const NoteMessage& a, const NoteMessage& b)
         {
            return a.pitch == b.pitch && a.channel == b.channel && a.scriptModuleId == b.scriptModuleId;
         };

         mBlockNoteCount = 0;
         int kept = 0;
         for (int i = 0; i < mPendingCount; ++i)
         {
            const NoteMessage note = scratch.pending[static_cast<size_t>(i)];
            const bool timed = clocked && note.timestamp > 0.0;
            bool wait = false;
            bool late = false;
            int offset = 0;
            if (timed)
            {
               const double frame =
               std::round((note.timestamp - hostTimeSeconds) * static_cast<double>(sampleRate));
               if (frame >= static_cast<double>(numSamples) && frame < horizonFrames)
                  wait = true;
               else if (frame < 0.0)
                  late = true;
               else if (frame < static_cast<double>(numSamples))
                  offset = static_cast<int>(frame);
               // Beyond the horizon (or not finite): now.
            }
            // A note waits while an earlier one for the same key does (a later block, or the cap).
            for (int k = 0; k < kept && !wait; ++k)
               wait = sameKey(scratch.pending[static_cast<size_t>(k)], note);
            if (!wait && mBlockNoteCount >= kMaxNoteEventsPerBlock)
               wait = true;
            if (wait)
            {
               scratch.pending[static_cast<size_t>(kept++)] = note;
               continue;
            }
            // ...and never starts before an earlier one for the same key in this block.
            for (int b = 0; b < mBlockNoteCount; ++b)
            {
               const BlockNote& earlier = scratch.blockNotes[static_cast<size_t>(b)];
               if (sameKey(earlier.note, note))
                  offset = std::max(offset, earlier.offset);
            }
            if (timed)
            {
               mTimedNotes.fetch_add(1, std::memory_order_relaxed);
               if (late)
                  mLateNotes.fetch_add(1, std::memory_order_relaxed);
            }
            // Sorted by offset; a note goes after the ones already at its offset.
            int at = mBlockNoteCount++;
            while (at > 0 && scratch.blockNotes[static_cast<size_t>(at - 1)].offset > offset)
            {
               scratch.blockNotes[static_cast<size_t>(at)] = scratch.blockNotes[static_cast<size_t>(at - 1)];
               --at;
            }
            scratch.blockNotes[static_cast<size_t>(at)] = { offset, note };
         }
         mPendingCount = kept;
      }

      void AudioGraphEngine::deliverNote(const AudioGraphSnapshot& graph, const NoteMessage& note)
      {
         const WasmNoteEvent event{ note.pitch, note.velocity, note.isNoteOn, note.channel };
         if (note.scriptModuleId >= 0)
         {
            // The Script adapter drains its queue when the plan reaches it in this piece.
            (void)pushScriptNote(note.scriptModuleId, event);
            return;
         }
         if (mMidiNoteCount >= kMaxNoteEventsPerBlock)
            return;
         mScratch->midiNoteScratch[static_cast<size_t>(mMidiNoteCount++)] = event;
         if (note.echoMidiOut && !graph.processPlan.performanceNotesProcessed)
            pushMidiOut(&event, 1);
      }

      void AudioGraphEngine::setMidiOutCapture(bool enabled)
      {
         mMidiOutCapture.store(enabled, std::memory_order_relaxed);
         if (!enabled)
         {
            // Queued notes belong to a port the user no longer wants; held bits would flush stale offs.
            mMidiOutReadIndex.store(mMidiOutWriteIndex.load(std::memory_order_relaxed), std::memory_order_relaxed);
            mScratch->midiOutHeld.fill(0);
         }
      }

      int AudioGraphEngine::drainMidiOut(MidiOutNote* dst, int maxNotes)
      {
         if (!dst || maxNotes <= 0)
            return 0;
         int count = 0;
         while (count < maxNotes)
         {
            const uint32_t read = mMidiOutReadIndex.load(std::memory_order_relaxed);
            if (read == mMidiOutWriteIndex.load(std::memory_order_acquire))
               break;
            dst[count++] = mScratch->midiOutRing[read];
            mMidiOutReadIndex.store((read + 1) % kMidiOutRingCapacity, std::memory_order_release);
         }
         return count;
      }

      void AudioGraphEngine::pushMidiOut(const WasmNoteEvent* notes, int count)
      {
         if (!notes || count <= 0 || !mMidiOutCapture.load(std::memory_order_relaxed))
            return;
         for (int i = 0; i < count; ++i)
         {
            const WasmNoteEvent& note = notes[i];
            // Graph bends (pitchbender) stay inside the patch; MIDI out carries notes only.
            if (note.pitch < 0 || note.pitch > 127 || isPitchBendEvent(note))
               continue;
            const uint32_t write = mMidiOutWriteIndex.load(std::memory_order_relaxed);
            const uint32_t nextWrite = (write + 1) % kMidiOutRingCapacity;
            if (nextWrite == mMidiOutReadIndex.load(std::memory_order_acquire))
            {
               // Full: keep the held bit as it was, so a dropped note-off is still flushed on stop.
               mMidiOutDropCount.fetch_add(1, std::memory_order_relaxed);
               continue;
            }
            const uint16_t channelBit = static_cast<uint16_t>(1u << (note.channel & 0x0f));
            uint16_t& held = mScratch->midiOutHeld[static_cast<size_t>(note.pitch)];
            if (note.isNoteOn)
               held = static_cast<uint16_t>(held | channelBit);
            else
               held = static_cast<uint16_t>(held & ~channelBit);
            mScratch->midiOutRing[write] = { note.pitch, note.velocity, note.isNoteOn, note.channel & 0x0f };
            mMidiOutWriteIndex.store(nextWrite, std::memory_order_release);
         }
      }

      void AudioGraphEngine::flushMidiOutHeld()
      {
         if (!mMidiOutCapture.load(std::memory_order_relaxed))
            return;
         for (int pitch = 0; pitch < 128; ++pitch)
         {
            for (int channel = 0; channel < 16; ++channel)
            {
               if ((mScratch->midiOutHeld[static_cast<size_t>(pitch)] & (1u << channel)) == 0)
                  continue;
               WasmNoteEvent off;
               off.pitch = pitch;
               off.velocity = 0.0f;
               off.isNoteOn = false;
               off.channel = channel;
               pushMidiOut(&off, 1);
            }
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
                                          float sampleRate,
                                          double hostTimeSeconds)
      {
         if (!output || numOutputChannels <= 0 || numSamples <= 0)
            return;
         AudioRtScope rtScope;

         const int hostChannels = std::min(numOutputChannels, 2);
         for (int ch = 0; ch < hostChannels; ++ch)
         {
            if (output[ch])
               std::memset(output[ch], 0, static_cast<size_t>(numSamples) * sizeof(float));
         }

         adoptSnapshot(graph);
         drainParamQueue(graph);
         mPublishedBpm.store(mLiveBpm, std::memory_order_relaxed);
         mPublishedPlaying.store(mLivePlaying, std::memory_order_relaxed);

         if (!mLivePlaying || sampleRate <= 0.0f)
         {
            if (!mLivePlaying)
            {
               mBeatPosition = 0.0;
               mPublishedBeat.store(0.0, std::memory_order_relaxed);
               resetEventSourceState();
               flushMidiOutHeld();
               mTransportWasPlaying = false;
               // Hard mute while stopped, but keep the ring empty: keys played now collapse to
               // their final state instead of replaying as a burst on Play.
               absorbQueuedNotesWhileStopped();
            }
            return;
         }

         const AudioProcessPlan& plan = graph.processPlan;
         if (!plan.valid || plan.hasCycle)
            return;

         if (numSamples > mPreparedBlockSize ||
             plan.bufferSlotCount > mPreparedAudioSlots ||
             plan.modulationSlotCount > mPreparedModulationSlots ||
             static_cast<int>(mInputScratch.size()) < numSamples)
            return;
         // Event and input scratch are sized by prepareRuntimeStates for this plan.
         if (mNoteSourceCounts.size() < plan.noteSourcePlans.size() ||
             mPulseSourceCounts.size() < plan.pulseSourceNodeIndices.size() ||
             mAudioInputScratch.size() < static_cast<size_t>(plan.maxAudioFanIn) ||
             mDownmixScratch.size() < static_cast<size_t>(plan.maxAudioFanIn) * static_cast<size_t>(mPreparedBlockSize))
         {
            mSkippedNodes.fetch_add(graph.nodes.size(), std::memory_order_relaxed);
            return;
         }

         if (!mTransportWasPlaying)
         {
            // Notes queued while nobody processed the engine (SDL / ring hosts pause with the
            // transport) are folded in now, then go out as one event per key.
            absorbQueuedNotesWhileStopped();
            mTransportWasPlaying = true;
         }
         if (mStoppedTouchedCount > 0)
         {
            if (!emitStoppedNotes())
               absorbQueuedNotesWhileStopped(); // keep later notes behind the remaining keys
         }
         else
         {
            collectBlockNotes(hostTimeSeconds, numSamples, sampleRate);
         }

         // One piece per distinct note offset; without timed notes that is the whole block.
         const auto& blockNotes = mScratch->blockNotes;
         int start = 0;
         int next = 0;
         int pieces = 0;
         while (start < numSamples)
         {
            mMidiNoteCount = 0;
            while (next < mBlockNoteCount && blockNotes[static_cast<size_t>(next)].offset <= start)
               deliverNote(graph, blockNotes[static_cast<size_t>(next++)].note);
            const int end = next < mBlockNoteCount ? blockNotes[static_cast<size_t>(next)].offset : numSamples;
            processPiece(graph, output, hostChannels, start, end - start, sampleRate);
            start = end;
            ++pieces;
         }
         if (pieces > 1)
            mSplitBlocks.fetch_add(1, std::memory_order_relaxed);
         mBlockNoteCount = 0;
      }

      void AudioGraphEngine::processPiece(const AudioGraphSnapshot& graph,
                                          float* const* output,
                                          int hostChannels,
                                          int offset,
                                          int numSamples,
                                          float sampleRate)
      {
         const AudioProcessPlan& plan = graph.processPlan;
         const size_t noteSourceCount = plan.noteSourcePlans.size();
         const size_t pulseSourceCount = plan.pulseSourceNodeIndices.size();
         advanceSmoothers(graph, numSamples, sampleRate);
         InputAudioBus::instance().consume(mInputScratch.data(), numSamples);

         const float bpm = mLiveBpm;
         const double beatStart = mBeatPosition;
         const double beatsPerBlock = static_cast<double>(numSamples) * bpm / (static_cast<double>(sampleRate) * 60.0);
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
         const size_t perSource = static_cast<size_t>(kMaxNoteEventsPerBlock);
         auto pulsesOf = [this](int slot)
         {
            return mPulseSourceScratch.data() + static_cast<size_t>(slot) * perSource;
         };
         auto notesOf = [this](int slot)
         {
            return mNoteSourceScratch.data() + static_cast<size_t>(slot) * perSource;
         };

         // Merges the pulses of `slots` (all below `limit`) into mergedPulses.
         auto mergePulses = [&](std::span<const int> slots, int limit, const WasmPulseEvent*& merged, int& mergedCount)
         {
            mergedCount = 0;
            for (int scratchSlot : slots)
            {
               // Plan order guarantees upstream slots were filled earlier this block.
               if (scratchSlot < 0 || scratchSlot >= limit)
                  continue;
               const int count = mPulseSourceCounts[static_cast<size_t>(scratchSlot)];
               const WasmPulseEvent* pulses = pulsesOf(scratchSlot);
               for (int i = 0; i < count && mergedCount < kMaxNoteEventsPerBlock; ++i)
                  scratch.mergedPulses[static_cast<size_t>(mergedCount++)] = pulses[i];
            }
            merged = mergedCount > 0 ? scratch.mergedPulses.data() : nullptr;
         };

         auto mergeNotes = [&](PlanRange range, int limit, const WasmNoteEvent*& merged, int& mergedCount)
         {
            mergedCount = 0;
            const auto slots = plan.noteScratchSlotsOf(range);
            const auto filters = plan.notePortFiltersOf(range);
            for (size_t cable = 0; cable < slots.size() && cable < filters.size(); ++cable)
            {
               const int upstream = slots[cable];
               const int portFilter = filters[cable];
               if (upstream < 0 || upstream >= limit)
                  continue;
               const int count = mNoteSourceCounts[static_cast<size_t>(upstream)];
               const WasmNoteEvent* upstreamNotes = notesOf(upstream);
               for (int i = 0; i < count && mergedCount < kMaxNoteEventsPerBlock; ++i)
               {
                  if (portFilter >= 0 && upstreamNotes[i].outputPort != portFilter)
                     continue;
                  scratch.mergedNotes[static_cast<size_t>(mergedCount++)] = upstreamNotes[i];
               }
            }
            merged = mergedCount > 0 ? scratch.mergedNotes.data() : nullptr;
         };

         std::fill(mPulseSourceCounts.begin(), mPulseSourceCounts.begin() + static_cast<std::ptrdiff_t>(pulseSourceCount), 0);
         for (int pulseSlot = 0; pulseSlot < static_cast<int>(pulseSourceCount); ++pulseSlot)
         {
            const int pulseNodeIndex = plan.pulseSourceNodeIndices[static_cast<size_t>(pulseSlot)];
            if (pulseNodeIndex < 0 || static_cast<size_t>(pulseNodeIndex) >= graph.nodes.size())
               continue;
            const auto& node = graph.nodes[static_cast<size_t>(pulseNodeIndex)];
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            void* state = nullptr;
            if (!adapter || !preparedState(pulseNodeIndex, node, state))
               continue;
            const WasmPulseEvent* upstreamPulses = nullptr;
            int upstreamPulseCount = 0;
            if (pulseSlot < static_cast<int>(plan.pulseSourcePlans.size()))
            {
               const EventSourcePlan& sourcePlan = plan.pulseSourcePlans[static_cast<size_t>(pulseSlot)];
               mergePulses(plan.pulseScratchSlotsOf(sourcePlan.pulseSources), pulseSlot, upstreamPulses,
                           upstreamPulseCount);
            }

            int& pulseCount = mPulseSourceCounts[static_cast<size_t>(pulseSlot)];
            pulseCount = 0;
            adapter->emitPulsesForBeatRange(state,
                                            liveParamsFor(graph, node),
                                            beatStart,
                                            beatEnd,
                                            pulsesOf(pulseSlot),
                                            kMaxNoteEventsPerBlock,
                                            pulseCount,
                                            upstreamPulses,
                                            upstreamPulseCount);
         }

         // Performance notes are snapped once, before processors and audio modules read them.
         quantizeNotes(scratch.midiNoteScratch.data(), mMidiNoteCount);

         std::fill(mNoteSourceCounts.begin(), mNoteSourceCounts.begin() + static_cast<std::ptrdiff_t>(noteSourceCount), 0);
         for (int seqSlot = 0; seqSlot < static_cast<int>(noteSourceCount); ++seqSlot)
         {
            const EventSourcePlan& sourcePlan = plan.noteSourcePlans[static_cast<size_t>(seqSlot)];
            if (sourcePlan.nodeIndex < 0 || static_cast<size_t>(sourcePlan.nodeIndex) >= graph.nodes.size())
               continue;
            const auto& node = graph.nodes[static_cast<size_t>(sourcePlan.nodeIndex)];
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            void* state = nullptr;
            if (!adapter || !preparedState(sourcePlan.nodeIndex, node, state))
               continue;

            WasmNoteEvent* emitted = notesOf(seqSlot);
            int& noteCount = mNoteSourceCounts[static_cast<size_t>(seqSlot)];
            noteCount = 0;

            if (sourcePlan.isProcessor)
            {
               // Upstream emitters ran earlier this block (plan order); no cable means the performance.
               WasmNoteProcessBlock block;
               block.beatStart = beatStart;
               block.beatEnd = beatEnd;
               block.bpm = bpm;
               block.sampleRate = sampleRate;
               block.numSamples = numSamples;
               block.hasNoteCable = sourcePlan.hasNoteCable;
               block.scale = &graph.musicScale;
               if (sourcePlan.hasNoteCable)
               {
                  mergeNotes(sourcePlan.noteSources, seqSlot, block.notes, block.noteCount);
               }
               else if (mMidiNoteCount > 0)
               {
                  block.notes = scratch.midiNoteScratch.data();
                  block.noteCount = mMidiNoteCount;
               }
               adapter->processNotes(state, liveParamsFor(graph, node), block, emitted, kMaxNoteEventsPerBlock,
                                     noteCount);
            }
            else
            {
               const WasmPulseEvent* sourcePulses = nullptr;
               int sourcePulseCount = 0;
               if (sourcePlan.hasPulseCable)
                  mergePulses(plan.pulseScratchSlotsOf(sourcePlan.pulseSources), static_cast<int>(pulseSourceCount),
                              sourcePulses, sourcePulseCount);

               adapter->emitNotesForBeatRange(state,
                                              liveParamsFor(graph, node),
                                              beatStart,
                                              beatEnd,
                                              emitted,
                                              kMaxNoteEventsPerBlock,
                                              noteCount,
                                              sourcePulses,
                                              sourcePulseCount);
            }

            if (adapter->quantizesNotesToScale())
               quantizeNotes(emitted, noteCount);
            if (sourcePlan.reportsToMidiOut)
               pushMidiOut(emitted, noteCount);
         }

         WasmAudioProcessContext modulationContext;
         modulationContext.sampleRate = sampleRate;
         modulationContext.numSamples = numSamples;
         modulationContext.blockStartTimeSeconds = mAudioTimeSeconds;
         modulationContext.beatStart = beatStart;
         modulationContext.beatEnd = beatEnd;
         modulationContext.bpm = bpm;
         modulationContext.inputAudio = mInputScratch.data();

         for (int lfoSlot = 0; lfoSlot < static_cast<int>(plan.modulationSourceNodeIndices.size()); ++lfoSlot)
         {
            const int lfoNodeIndex = plan.modulationSourceNodeIndices[static_cast<size_t>(lfoSlot)];
            if (lfoNodeIndex < 0 || static_cast<size_t>(lfoNodeIndex) >= graph.nodes.size())
               continue;
            const auto& node = graph.nodes[static_cast<size_t>(lfoNodeIndex)];
            float* buffer = modulationSlotBuffer(lfoSlot, numSamples);
            if (!buffer)
               continue;
            std::memset(buffer, 0, static_cast<size_t>(numSamples) * sizeof(float));
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            void* state = nullptr;
            if (!adapter)
               continue;
            if (!preparedState(lfoNodeIndex, node, state))
            {
               mSkippedNodes.fetch_add(1, std::memory_order_relaxed);
               continue;
            }
            // Note- or pulse-triggered sources (envelope, ramper) read the emitters that already ran.
            WasmAudioProcessContext sourceContext = modulationContext;
            if (lfoSlot < static_cast<int>(plan.modulationSourcePlans.size()))
            {
               const EventSourcePlan& sourcePlan = plan.modulationSourcePlans[static_cast<size_t>(lfoSlot)];
               sourceContext.hasNoteCable = sourcePlan.hasNoteCable;
               sourceContext.hasPulseCable = sourcePlan.hasPulseCable;
               if (sourcePlan.hasNoteCable)
               {
                  mergeNotes(sourcePlan.noteSources, static_cast<int>(noteSourceCount), sourceContext.notes,
                             sourceContext.noteCount);
               }
               else if (mMidiNoteCount > 0)
               {
                  sourceContext.notes = scratch.midiNoteScratch.data();
                  sourceContext.noteCount = mMidiNoteCount;
               }
               if (sourcePlan.hasPulseCable)
                  mergePulses(plan.pulseScratchSlotsOf(sourcePlan.pulseSources), static_cast<int>(pulseSourceCount),
                              sourceContext.pulses, sourceContext.pulseCount);
            }
            adapter->processMono(state, liveParamsFor(graph, node), buffer, sourceContext);
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
            void* state = nullptr;
            if (stepAdapter && !preparedState(step.nodeIndex, node, state))
            {
               // Never seen by prepareRuntimeStates: output silence rather than allocate here.
               std::memset(channels[0], 0, static_cast<size_t>(numSamples) * sizeof(float));
               mSlotChannels[static_cast<size_t>(step.outBufferSlot)] = 1;
               for (int auxSlot : plan.auxOutputSlotsOf(step))
               {
                  if (float* auxLeft = audioSlotBuffer(auxSlot, 0, numSamples))
                  {
                     std::memset(auxLeft, 0, static_cast<size_t>(numSamples) * sizeof(float));
                     mSlotChannels[static_cast<size_t>(auxSlot)] = 1;
                  }
               }
               mSkippedNodes.fetch_add(1, std::memory_order_relaxed);
               continue;
            }
            const bool sumInputs = !stepAdapter || stepAdapter->sumsAudioInputs();
            const auto inputSlots = plan.audioInputSlotsOf(step);
            const auto inputPorts = plan.audioInputPortsOf(step);
            int audioInputCount = 0;
            int inputChannels = 1;
            for (size_t inputIndex = 0; inputIndex < inputSlots.size(); ++inputIndex)
            {
               const int inputSlot = inputSlots[inputIndex];
               const float* left = audioSlotBuffer(inputSlot, 0, numSamples);
               if (!left)
                  continue;
               // Mono upmix is free: a mono slot hands out its L buffer as R.
               WasmAudioInput& input = mAudioInputScratch[static_cast<size_t>(audioInputCount)];
               input.numChannels = audioSlotChannels(inputSlot);
               input.channels[0] = left;
               input.channels[1] = input.numChannels == 2 ? audioSlotBuffer(inputSlot, 1, numSamples) : left;
               input.port = inputPorts[inputIndex];
               const size_t mixIndex = step.audioInputs.begin + inputIndex;
               const PlanChannelMix mix =
               mixIndex < plan.audioInputMix.size() ? plan.audioInputMix[mixIndex] : PlanChannelMix::AsIs;
               if (mix == PlanChannelMix::ToMono && input.numChannels == 2)
               {
                  float* mono = mDownmixScratch.data() + static_cast<size_t>(audioInputCount) * static_cast<size_t>(mPreparedBlockSize);
                  for (int i = 0; i < numSamples; ++i)
                     mono[i] = (input.channels[0][i] + input.channels[1][i]) * 0.5f;
                  input.channels[0] = mono;
                  input.channels[1] = mono;
                  input.numChannels = 1;
               }
               else if (mix == PlanChannelMix::ToStereo)
               {
                  input.numChannels = 2;
               }
               ++audioInputCount;
               inputChannels = std::max(inputChannels, input.numChannels);
            }

            const WasmNoteEvent* moduleNotes = nullptr;
            int moduleNoteCount = 0;
            if (step.hasNoteCable)
            {
               mergeNotes(step.noteSources, static_cast<int>(noteSourceCount), moduleNotes, moduleNoteCount);
            }
            else if (globalMidiNotes)
            {
               moduleNotes = globalMidiNotes;
               moduleNoteCount = mMidiNoteCount;
            }

            const WasmPulseEvent* modulePulses = nullptr;
            int modulePulseCount = 0;
            if (step.hasPulseCable)
               mergePulses(plan.pulseScratchSlotsOf(step.pulseSources), static_cast<int>(pulseSourceCount), modulePulses,
                           modulePulseCount);

            WasmAudioProcessContext context;
            context.sampleRate = sampleRate;
            context.numSamples = numSamples;
            context.blockStartTimeSeconds = blockStart;
            context.beatStart = beatStart;
            context.beatEnd = beatEnd;
            context.bpm = bpm;
            context.inputAudio = mInputScratch.data();
            context.noteFrequencies = graph.musicScale.noteFrequency.data();
            context.hasNoteCable = step.hasNoteCable;
            context.hasPulseCable = step.hasPulseCable;
            context.hasAudioInput = !step.audioInputs.empty();
            context.audioInputs = audioInputCount > 0 ? mAudioInputScratch.data() : nullptr;
            context.audioInputCount = audioInputCount;
            context.inputChannels = inputChannels;

            const void* params = liveParamsFor(graph, node);
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
                  accumulateAudioInput(mAudioInputScratch[static_cast<size_t>(c)], channels, numChannels, numSamples);
            }
            mSlotChannels[static_cast<size_t>(step.outBufferSlot)] = static_cast<uint8_t>(numChannels);
            // Extra Audio outputs (send): zeroed, as wide as the main output.
            float* auxChannels[2 * kMaxAuxAudioOutputs] = {};
            int auxCount = 0;
            for (int auxSlot : plan.auxOutputSlotsOf(step))
            {
               if (auxCount >= kMaxAuxAudioOutputs)
                  break;
               float* auxLeft = audioSlotBuffer(auxSlot, 0, numSamples);
               float* auxRight = numChannels == 2 ? audioSlotBuffer(auxSlot, 1, numSamples) : nullptr;
               if (!auxLeft || (numChannels == 2 && !auxRight))
                  break;
               std::memset(auxLeft, 0, static_cast<size_t>(numSamples) * sizeof(float));
               if (auxRight)
                  std::memset(auxRight, 0, static_cast<size_t>(numSamples) * sizeof(float));
               auxChannels[auxCount * 2] = auxLeft;
               auxChannels[auxCount * 2 + 1] = auxRight;
               mSlotChannels[static_cast<size_t>(auxSlot)] = static_cast<uint8_t>(numChannels);
               ++auxCount;
            }
            if (auxCount > 0)
            {
               context.auxOutputs = auxChannels;
               context.auxOutputCount = auxCount;
            }
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

            const auto modSlots = plan.modulationSlotsOf(step);
            if (!modSlots.empty())
            {
               std::memset(mSummedModulation.data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
               for (int modSlot : modSlots)
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
               stepAdapter->processAudio(state, params, channels, context);
         }

         const int outChannels = audioSlotChannels(plan.outputBufferSlot);
         const float* outBuffers[2] = { audioSlotBuffer(plan.outputBufferSlot, 0, numSamples),
                                        audioSlotBuffer(plan.outputBufferSlot, outChannels == 2 ? 1 : 0, numSamples) };
         if (!outBuffers[0])
            return;

         // The Output limiter normally makes this clamp a no-op; it remains the last guard when the limiter is off.
         if (hostChannels == 1 && outChannels == 2 && output[0])
         {
            float* out = output[0] + offset;
            for (int i = 0; i < numSamples; ++i)
               out[i] = clampFloat((outBuffers[0][i] + outBuffers[1][i]) * 0.5f, -1.0f, 1.0f);
         }
         else
         {
            for (int ch = 0; ch < hostChannels; ++ch)
            {
               if (!output[ch])
                  continue;
               float* out = output[ch] + offset;
               for (int i = 0; i < numSamples; ++i)
                  out[i] = clampFloat(outBuffers[ch][i], -1.0f, 1.0f);
            }
         }
         mAudioTimeSeconds += static_cast<double>(numSamples) / sampleRate;
      }

   } // namespace wasm
} // namespace bespoke
