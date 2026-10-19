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

         void push(int pitch, float velocity, bool isNoteOn, int channel = 0, int outputPort = 0)
         {
            if (!valid() || count >= max)
               return;
            WasmNoteEvent& event = out[count++];
            event.pitch = pitch;
            event.velocity = velocity;
            event.isNoteOn = isNoteOn;
            event.channel = channel;
            event.outputPort = outputPort;
         }
      };

      inline int clampMidiPitch(int pitch)
      {
         return pitch < 0 ? 0 : (pitch > 127 ? 127 : pitch);
      }

   } // namespace wasm
} // namespace bespoke
