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
            /**
             * When the note starts, in seconds on the host clock processBlock is given (AudioContext
             * time in the AudioWorklet engine). 0, or a host that passes no clock, plays it at the
             * start of the next block; a time the engine already rendered plays there too (late).
             */
            double timestamp = 0.0;
            int channel = 0;
            /**
             * -1: a performance note (QWERTY, MIDI in, a Script module without Note cables). Otherwise
             * a note for that Script module's own Note cables, put in its queue (ScriptNoteQueue) at
             * the note's frame so the Script adapter emits it there.
             */
            int scriptModuleId = -1;
            /**
             * A performance note no host echoes to the MIDI output itself (Script notes sent straight
             * to the engine): reported there unless a Note processor owns the performance.
             */
            bool echoMidiOut = false;
         };

         /** A note the graph played, for the selected MIDI output (see setMidiOutCapture). */
         struct MidiOutNote
         {
            int pitch = 60;
            float velocity = 0.0f;
            bool isNoteOn = false;
            int channel = 0;
         };

         static constexpr int kMaxNoteRingCapacity = 256;
         /** Timed notes drained from the ring and waiting for their block (the rest stay in the ring). */
         static constexpr int kMaxPendingNotes = kMaxNoteRingCapacity;
         /** A timestamp further ahead than this plays now: a host clock mix-up, not a schedule. */
         static constexpr double kMaxScheduleAheadSeconds = 1.0;
         static constexpr int kMidiOutRingCapacity = 512;
         static constexpr int kMaxNoteEventsPerBlock = 64;
         static constexpr int kMaxBlockSize = 2048;
         static constexpr int kParamQueueCapacity = 1024;
         /** Applied changes remembered for re-applying over a snapshot built before them. */
         static constexpr int kRecentParamCapacity = 256;
         static constexpr int kMaxParamSmoothers = 64;

         struct Stats
         {
            uint64_t prepareCount = 0; // prepareRuntimeStates calls
            uint64_t adoptCount = 0; // tracked snapshots copied into the live params
            uint64_t paramChangesApplied = 0;
            uint64_t paramChangesDropped = 0; // queue full
            uint64_t paramChangesStale = 0; // older than the adopted snapshot
            uint64_t skippedNodes = 0; // processed before prepareRuntimeStates saw them
            uint64_t timedNotes = 0; // notes with a timestamp, played on a host clock
            uint64_t lateNotes = 0; // timed notes whose frame was already rendered (played at a block start)
            uint64_t splitBlocks = 0; // blocks rendered in pieces so a note starts on its frame
         };

         AudioGraphEngine();
         ~AudioGraphEngine();

         AudioGraphEngine(const AudioGraphEngine&) = delete;
         AudioGraphEngine& operator=(const AudioGraphEngine&) = delete;

         /**
          * Producer side of the SPSC note ring. A full ring drops this (the newest) note and
          * counts it in noteDropCount(); the producer never moves the consumer's read index.
          */
         void queueNote(const NoteMessage& note);
         uint64_t noteDropCount() const { return mNoteDropCount.load(std::memory_order_relaxed); }
         /**
          * Pops up to `maxNotes` notes this engine has not rendered: first the final state of every
          * key touched while the transport was stopped (see absorbQueuedNotesWhileStopped), then
          * timed notes waiting for their block, then the ring, oldest first. Only while nothing
          * processes the engine: the UI module hands them to the AudioWorklet engine when that takes
          * over playback (a chord held before Play).
          */
         int takeQueuedNotes(NoteMessage* dst, int maxNotes);

         /**
          * Consumer side; call only from the thread that runs processBlock, while the transport
          * is stopped. Folds queued notes into a per-key final state (held + velocity), so keys
          * played while stopped never replay as a burst on Play: the first playing block emits
          * one event per touched key instead (note-on if still held, else note-off). Timing is
          * dropped. Notes for a Script module's cables are not folded: note-offs go to its queue,
          * note-ons are dropped (a stopped Script module emits nothing). processBlock does this
          * itself while it is called stopped; a host that stops calling it with the transport
          * (SDL, main-thread ring) calls this after queueing so the ring cannot fill.
          */
         void absorbQueuedNotesWhileStopped();

         /**
          * While enabled, notes from the graph's note emitters (sequencers, drums, script, arp,
          * and any processor whose output is not itself cabled into another processor) are
          * queued for drainMidiOut. Stopping the transport queues note-offs for everything
          * still sounding on the output. Off by default so nothing queues with no consumer.
          */
         void setMidiOutCapture(bool enabled);
         /** UI thread: copy up to `maxNotes` queued notes in delivery order. */
         int drainMidiOut(MidiOutNote* dst, int maxNotes);
         uint64_t midiOutDropCount() const { return mMidiOutDropCount.load(std::memory_order_relaxed); }

         /**
          * Producer side of the SPSC param queue (the thread that queues notes). Applied at the
          * start of the next block over the adopted snapshot's params; changes with seq at or below
          * the snapshot's paramSeq are skipped. Returns false when the queue is full (the caller
          * republishes instead).
          */
         [[nodiscard]] bool queueParamChange(const ParamChange& change);
         Stats stats() const;
         /**
          * Adopts `graph` and applies the queued param changes now. For hosts that stop calling
          * processBlock with the transport (SDL, main-thread ring), on the thread that would run it,
          * so a stopped session cannot fill the queue.
          */
         void applyQueuedParams(const AudioGraphSnapshot& graph);

         /** Grow buffer arenas on the UI thread (init / buffer-size changes). */
         void prepareForBlock(int maxBlockSize, int maxAudioSlots, int maxModulationSlots);

         /**
          * Zero the transport clock, drop queued input notes and all module runtime state (offline /
          * determinism / a new patch). Queues note-offs for notes still sounding on the MIDI output.
          */
         void resetClock();

         /**
          * UI thread, after each topology publish: create runtime state for new nodes, reset
          * the state of nodes that left the graph, and size the plan-dependent scratch (event
          * buffers, live params, input lists), so processBlock never allocates. A node this has
          * not seen is skipped by processBlock (counted in Stats::skippedNodes). Assumes the
          * audio graph is not running concurrently (main-thread audio, or the worklet thread
          * that also runs processBlock).
          */
         void prepareRuntimeStates(const AudioGraphSnapshot& graph);

         /** Transport position in beats at the end of the last processed block (0 when stopped). */
         double beatPosition() const { return mPublishedBeat.load(std::memory_order_relaxed); }
         /** Tempo and play state the last processed block ran at (snapshot plus queued changes). */
         float transportBpm() const { return mPublishedBpm.load(std::memory_order_relaxed); }
         bool transportPlaying() const { return mPublishedPlaying.load(std::memory_order_relaxed); }

         /**
          * Largest |sample| reaching the Output module since the last call, per
          * channel (0 = L, 1 = R). Measured on the summed input, before the
          * Output limiter, so values above 1 mean the patch is clipping.
          * Single reader (UI thread); resets on read.
          */
         float takeOutputPeak(int channel);

         /**
          * Stopped transport is a hard mute: the block is silent and nothing (delay or reverb
          * tails included) keeps rendering until Play. Sequencers reset, held MIDI-out notes get
          * note-offs, and performance notes are folded per key (absorbQueuedNotesWhileStopped).
          *
          * `hostTimeSeconds` is the block's first frame on the clock NoteMessage::timestamp uses.
          * With it, a timed note starts on its own frame: the block is rendered in pieces split
          * there (each piece runs the whole plan), and a note due in a later block waits for it.
          * A note never goes out before an earlier-queued one for the same key and destination.
          * Negative (SDL, main-thread ring, offline): every note plays at the block start.
          */
         void processBlock(const AudioGraphSnapshot& graph,
                           float* const* output,
                           int numOutputChannels,
                           int numSamples,
                           float sampleRate,
                           double hostTimeSeconds = -1.0);

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

         struct NodeRuntime
         {
            int moduleId = -1;
            int typeIndex = -1;
            int slotIndex = -1; // into mRuntimeSlots; -1 when the adapter has no state
         };

         struct ParamSmoother
         {
            uint32_t liveOffset = 0; // byte offset of the float in mLiveParams
            int nodeIndex = -1;
            float current = 0.0f;
            float target = 0.0f;
            float smoothingMs = 0.0f;
            bool active = false;
         };

         void* stateFor(const AudioGraphNode& node);
         /** Prepared runtime state of `node` (no allocation); false when it was never prepared. */
         bool preparedState(int nodeIndex, const AudioGraphNode& node, void*& state);
         void adoptSnapshot(const AudioGraphSnapshot& graph);
         void drainParamQueue(const AudioGraphSnapshot& graph);
         void applyParamChange(const AudioGraphSnapshot& graph, const ParamChange& change, bool smooth);
         void advanceSmoothers(const AudioGraphSnapshot& graph, int numSamples, float sampleRate);
         const void* liveParamsFor(const AudioGraphSnapshot& graph, const AudioGraphNode& node) const;
         /** Live params to apply a change to; nullptr for an untracked snapshot (read-only). */
         void* writableLiveParamsFor(const AudioGraphSnapshot& graph, const AudioGraphNode& node);
         /** A note due in the current block, `offset` frames after its start. */
         struct BlockNote
         {
            int offset = 0;
            NoteMessage note;
         };

         void destroySlot(RuntimeSlot& slot);
         void resetEventSourceState();
         bool popRingNote(NoteMessage& note);
         void foldNoteWhileStopped(const NoteMessage& note);
         /**
          * Moves ring notes into the pending list, then picks the ones due in this block (all of
          * them without a host clock) into mBlockNotes, sorted by offset.
          */
         void collectBlockNotes(double hostTimeSeconds, int numSamples, float sampleRate);
         /** Writes up to the per-block limit of collapsed stopped-transport notes; true when all went out. */
         bool emitStoppedNotes();
         /** Hands a due note to the performance list of the piece that starts at it, or its Script queue. */
         void deliverNote(const AudioGraphSnapshot& graph, const NoteMessage& note);
         /** Renders frames [offset, offset + numSamples) of the block with the notes delivered so far. */
         void processPiece(const AudioGraphSnapshot& graph,
                           float* const* output,
                           int hostChannels,
                           int offset,
                           int numSamples,
                           float sampleRate);
         void pushMidiOut(const WasmNoteEvent* notes, int count);
         void flushMidiOutHeld();
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

         std::vector<NodeRuntime> mNodeRuntime; // by node index of the last prepared snapshot
         // Plan-sized event scratch (prepareRuntimeStates): kMaxNoteEventsPerBlock per emitter.
         std::vector<WasmNoteEvent> mNoteSourceScratch;
         std::vector<int> mNoteSourceCounts;
         std::vector<WasmPulseEvent> mPulseSourceScratch;
         std::vector<int> mPulseSourceCounts;
         std::vector<WasmAudioInput> mAudioInputScratch;
         std::vector<float> mDownmixScratch; // one block per audio input summed to a Mono port

         // Live parameters: the adopted snapshot's arena with queued changes applied.
         std::vector<uint8_t> mLiveParams;
         bool mLiveParamsValid = false;
         const AudioGraphSnapshot* mAdoptedGraph = nullptr;
         uint64_t mAdoptedGeneration = 0;
         uint32_t mAdoptedParamSeq = 0;
         bool mLivePlaying = false;
         float mLiveBpm = 120.0f;
         std::atomic<uint32_t> mParamWriteIndex{ 0 };
         std::atomic<uint32_t> mParamReadIndex{ 0 };
         int mRecentParamNext = 0;
         int mRecentParamCount = 0;
         std::atomic<uint64_t> mPrepareCount{ 0 };
         std::atomic<uint64_t> mAdoptCount{ 0 };
         std::atomic<uint64_t> mParamChangesApplied{ 0 };
         std::atomic<uint64_t> mParamChangesDropped{ 0 };
         std::atomic<uint64_t> mParamChangesStale{ 0 };
         std::atomic<uint64_t> mSkippedNodes{ 0 };

         std::unique_ptr<ProcessScratch> mScratch;
         std::atomic<uint32_t> mNoteWriteIndex{ 0 };
         std::atomic<uint32_t> mNoteReadIndex{ 0 };
         std::atomic<uint64_t> mNoteDropCount{ 0 };
         std::atomic<bool> mMidiOutCapture{ false };
         std::atomic<uint32_t> mMidiOutWriteIndex{ 0 };
         std::atomic<uint32_t> mMidiOutReadIndex{ 0 };
         std::atomic<uint64_t> mMidiOutDropCount{ 0 };

         int mMidiNoteCount = 0;
         int mPendingCount = 0;
         int mBlockNoteCount = 0;
         std::atomic<uint64_t> mTimedNotes{ 0 };
         std::atomic<uint64_t> mLateNotes{ 0 };
         std::atomic<uint64_t> mSplitBlocks{ 0 };
         /** False until the first playing block (and again after a stopped one): absorb, then emit. */
         bool mTransportWasPlaying = false;
         /** Keys whose final stopped-transport state is still to be emitted. */
         int mStoppedTouchedCount = 0;

         double mAudioTimeSeconds = 0.0;
         double mBeatPosition = 0.0;
         std::atomic<double> mPublishedBeat{ 0.0 };
         std::atomic<float> mPublishedBpm{ 120.0f };
         std::atomic<bool> mPublishedPlaying{ false };
         std::atomic<float> mOutputPeak[2]{ { 0.0f }, { 0.0f } };
      };

   } // namespace wasm
} // namespace bespoke
