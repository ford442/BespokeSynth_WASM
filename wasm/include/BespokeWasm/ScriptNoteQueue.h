/**
 * BespokeSynth WASM - Lock-free note queues for Script modules
 *
 * The TS script host (a worker, via the main thread) pushes notes on the UI
 * thread; the Script adapter drains them in emitNotesForBeatRange on the audio
 * side. Fixed tables only: nothing here allocates after static init.
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
      constexpr int kMaxScriptQueues = 16;
      constexpr int kScriptQueueCapacity = 128;

      /** UI thread. false when the queue is full or every slot is taken. */
      bool pushScriptNote(int moduleId, const WasmNoteEvent& note);
      /** Audio thread. Returns the number of notes copied into `out`. */
      int drainScriptNotes(int moduleId, WasmNoteEvent* out, int maxNotes);
      /** Drop queued notes and free the module's slot (script stopped / module deleted). */
      void clearScriptNotes(int moduleId);
      void resetScriptNoteQueues();
   } // namespace wasm
} // namespace bespoke
