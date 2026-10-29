/**
 * BespokeSynth WASM - UI module side of the AudioWorklet engine (Option A)
 *
 * While the host runs the graph in the AudioWorklet's own WASM instance
 * (bespoke_set_audio_remote), this module's AudioGraphEngine stays idle. Edits
 * still publish snapshots here; these helpers forward what the worklet needs
 * (notes, script notes, "a graph was published", "a patch was loaded") to the
 * host, which owns the SharedArrayBuffer rings and the worklet port.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphEngine.h"

namespace bespoke
{
   namespace wasm
   {
      /** True while the AudioWorklet engine plays the graph instead of this module. */
      bool audioRemoteActive();

      /** A performance note (MIDI in, QWERTY, uncabled script): to whichever engine plays. */
      void queuePerformanceNote(const AudioGraphEngine::NoteMessage& note);
      /** A Script module's cabled note. false when the local queue is full. */
      bool queueScriptNote(int moduleId, const WasmNoteEvent& note);
      void clearScriptNoteQueue(int moduleId);
      /** ModuleCanvas param sink: to whichever engine plays. false when its queue is full. */
      [[nodiscard]] bool queueParamChange(const ParamChange& change);

      /** Snapshot listener, remote mode: the host encodes and posts the latest graph. */
      void notifyAudioGraphPublished();
      /** Patch swap, remote mode: the worklet resets clock and state with the next graph. */
      void notifyAudioGraphReset();

      /** Transport beat as last reported by whichever engine plays. */
      double playingTransportBeat();
      /** Output peak since the last call (0 = L, 1 = R), from whichever engine plays. */
      float takePlayingOutputPeak(int channel);

   } // namespace wasm
} // namespace bespoke
