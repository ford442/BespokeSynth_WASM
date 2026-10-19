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
         /** a = pitch | channel << 8 | isNoteOn << 12, b = velocity bits. */
         Note = 1,
         /** a = moduleId, b = pitch | isNoteOn << 8, c = velocity bits. */
         ScriptNote = 2,
         /** a = moduleId: drop that Script module's queued notes. */
         ScriptClear = 3,
      };

      constexpr int kAudioEngineEventWords = 4;

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
         kStatusSlotCount
      };

   } // namespace wasm
} // namespace bespoke
