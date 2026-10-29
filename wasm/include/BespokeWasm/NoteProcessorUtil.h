/**
 * BespokeSynth WASM - Shared helpers for NoteProcessor adapters
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      /** Appends to a processNotes output buffer; events past `max` are dropped. */
      struct NoteWriter
      {
         WasmNoteEvent* out;
         int max;
         int& count;

         NoteWriter(WasmNoteEvent* outNotes, int maxNotes, int& outCount)
         : out(outNotes)
         , max(maxNotes)
         , count(outCount)
         {
            count = 0;
         }

         bool valid() const { return out != nullptr && max > 0; }

         /** A note on / off; a note-on starts at `pitchBend` semitones. */
         void push(int pitch, float velocity, bool isNoteOn, int channel = 0, int outputPort = 0, float pitchBend = 0.0f)
         {
            if (!valid() || count >= max)
               return;
            // The buffer is reused every block: write every field.
            WasmNoteEvent& event = out[count++];
            event = WasmNoteEvent{};
            event.pitch = pitch;
            event.velocity = velocity;
            event.isNoteOn = isNoteOn;
            event.channel = channel;
            event.outputPort = outputPort;
            event.pitchBend = pitchBend;
         }

         /** A new bend for the note sounding at `pitch` (NoteEventKind::PitchBend). */
         void pushBend(int pitch, float semitones, int channel = 0, int outputPort = 0)
         {
            if (!valid() || count >= max)
               return;
            WasmNoteEvent& event = out[count++];
            event = WasmNoteEvent{};
            event.pitch = pitch;
            event.channel = channel;
            event.outputPort = outputPort;
            event.kind = NoteEventKind::PitchBend;
            event.pitchBend = semitones;
         }
      };

      inline int clampMidiPitch(int pitch)
      {
         return pitch < 0 ? 0 : (pitch > 127 ? 127 : pitch);
      }

   } // namespace wasm
} // namespace bespoke
