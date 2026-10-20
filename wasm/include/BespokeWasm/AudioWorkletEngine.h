/**
 * BespokeSynth WASM - AudioWorklet engine (Option A, docs/wasm-audio.md)
 *
 * The audio-only module (BespokeSynthWASM_audio) is instantiated a second time
 * inside the AudioWorklet and runs AudioGraphEngine::processBlock there, so a long
 * canvas frame on the main thread cannot starve the speakers. It never sees the
 * canvas: the UI module publishes encoded snapshots (AudioGraphSnapshotCodec),
 * decoded samples, and performance/script notes; this module renders blocks and
 * reports transport beat, peaks, MIDI-out notes and health back.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstdint>

namespace bespoke
{
   namespace wasm
   {
      /**
       * Event records the main thread writes into the SharedArrayBuffer event ring and
       * the worklet copies into bespoke_audio_apply_events, four int32 words each.
       * Floats travel as their IEEE bits. Mirrors src/audio/engineShared.ts (EVENT_*).
       */
      enum class AudioEngineEventType : int32_t
      {
         None = 0,
         /**
          * A performance note. a = pitch | channel << 8 | isNoteOn << 12 | timed << 13, b = velocity
          * bits, c = when timed: the low 32 bits of the AudioContext frame it starts on.
          */
         Note = 1,
         /**
          * A Script note the UI module routed to the module's own cables (relayed through the
          * main thread). a = moduleId, b = pitch | isNoteOn << 8, c = velocity bits.
          */
         ScriptNote = 2,
         /**
          * a = moduleId: the Script module stopped. Drops its queued notes, then releases every
          * note it still holds through its direct ring.
          */
         ScriptClear = 3,
         /**
          * A control change (ParamChange): a = (moduleId + 1) & 0xFFFFF | paramId << 20
          * (moduleId -1 addresses a GlobalParam), b = value bits, c = seq.
          */
         ParamChange = 4,
         /**
          * A note a Script worker wrote straight into its own ring (the processor fills in the
          * module it registered the ring for): a = (moduleId + 1) & 0xFFFFF | pitch << 20 |
          * isNoteOn << 27 | timed << 28, b = velocity bits, c = when timed: the low 32 bits of
          * its start frame. The engine routes it like bespoke_script_note: to the module's Note
          * cables when it has any, else played like a keyboard (and echoed to MIDI out).
          */
         ScriptDirectNote = 5,
      };

      constexpr int kAudioEngineEventWords = 4;
      /** Note records: the start frame in word 3 is valid. */
      constexpr int32_t kAudioEngineNoteTimed = 1 << 13;
      constexpr int32_t kAudioEngineScriptDirectTimed = 1 << 28;

      /** Slots of bespoke_audio_read_status, mirrored by src/audio/engineShared.ts. */
      enum AudioEngineStatusSlot
      {
         kStatusBeat = 0,
         kStatusCallbackCount,
         kStatusUnderrunCount,
         kStatusLastProcessMs,
         kStatusMaxProcessMs,
         kStatusCpuLoad,
         kStatusLastIntervalMs,
         kStatusNoteDropCount,
         kStatusHeapBytes,
         kStatusMidiOutDropCount,
         kStatusOutputPeakL,
         kStatusOutputPeakR,
         kStatusInputPeak,
         kStatusSnapshotCount,
         kStatusBpm,
         kStatusPlaying,
         /** Notes that came with a start frame, and those whose frame was already rendered. */
         kStatusTimedNotes,
         kStatusLateNotes,
         kStatusSplitBlocks,
         /** Script worker notes that came straight from a worker ring. */
         kStatusDirectScriptNotes,
         /** AudioRtViolations; allocations and frees stay 0 outside the Debug flavor. */
         kStatusRtAllocations,
         kStatusRtFrees,
         kStatusRtLocks,
         kStatusRtCountsAllocations,
         /** AudioThreadScope the engine's blocks run in (3 = AudioWorkletGlobalScope). */
         kStatusEngineScope,
         /** AudioHealthSnapshot::blockTimeHistogram, kAudioBlockTimeBuckets slots. */
         kStatusBlockTimeHistogram,
         kStatusSlotCount = kStatusBlockTimeHistogram + 8
      };

   } // namespace wasm
} // namespace bespoke
