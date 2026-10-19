/**
 * BespokeSynth WASM - UI module side of the AudioWorklet engine
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioRemote.h"
#include "BespokeWasm/AudioWorkletEngine.h"
#include "BespokeWasm/ScriptNoteQueue.h"
#include "BespokeWasm/WasmRuntimeState.h"

#include <cstring>
#include <emscripten.h>

// The host installs Module.bespokeAudioRemote while the worklet engine is live
// (src/audio/workletEngineBackend.ts); without it these are no-ops.
// clang-format off
EM_JS(void, bespoke_js_remote_audio_event, (int type, int a, int b, int c), {
   var remote = Module['bespokeAudioRemote'];
   if (remote) remote.event(type, a, b, c);
});

EM_JS(void, bespoke_js_remote_audio_graph, (int reset), {
   var remote = Module['bespokeAudioRemote'];
   if (remote) remote.graphChanged(reset !== 0);
});
// clang-format on

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         int floatBits(float value)
         {
            int bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
         }

         void sendEvent(AudioEngineEventType type, int a, int b, int c)
         {
            bespoke_js_remote_audio_event(static_cast<int>(type), a, b, c);
         }
      }

      bool audioRemoteActive()
      {
         return runtimeState().remoteAudioActive;
      }

      void queuePerformanceNote(const AudioGraphEngine::NoteMessage& note)
      {
         auto& state = runtimeState();
         if (state.remoteAudioActive)
         {
            const int packed = (note.pitch & 0x7f) | ((note.channel & 0x0f) << 8) | ((note.isNoteOn ? 1 : 0) << 12);
            sendEvent(AudioEngineEventType::Note, packed, floatBits(note.velocity), 0);
            return;
         }
         if (state.audioGraphEngine)
            state.audioGraphEngine->queueNote(note);
      }

      bool queueScriptNote(int moduleId, const WasmNoteEvent& note)
      {
         if (runtimeState().remoteAudioActive)
         {
            const int packed = (note.pitch & 0x7f) | ((note.isNoteOn ? 1 : 0) << 8);
            sendEvent(AudioEngineEventType::ScriptNote, moduleId, packed, floatBits(note.velocity));
            return true;
         }
         return pushScriptNote(moduleId, note);
      }

      void clearScriptNoteQueue(int moduleId)
      {
         // Both sides: the local queue may still hold notes from before the worklet took over.
         clearScriptNotes(moduleId);
         if (runtimeState().remoteAudioActive)
            sendEvent(AudioEngineEventType::ScriptClear, moduleId, 0, 0);
      }

      void notifyAudioGraphPublished()
      {
         bespoke_js_remote_audio_graph(0);
      }

      void notifyAudioGraphReset()
      {
         bespoke_js_remote_audio_graph(1);
      }

      double playingTransportBeat()
      {
         const auto& state = runtimeState();
         if (state.remoteAudioActive)
            return state.remoteBeat;
         return state.audioGraphEngine ? state.audioGraphEngine->beatPosition() : 0.0;
      }

      float takePlayingOutputPeak(int channel)
      {
         auto& state = runtimeState();
         if (channel < 0 || channel > 1)
            return 0.0f;
         if (state.remoteAudioActive)
         {
            const float peak = state.remoteOutputPeak[channel];
            state.remoteOutputPeak[channel] = 0.0f;
            return peak;
         }
         return state.audioGraphEngine ? state.audioGraphEngine->takeOutputPeak(channel) : 0.0f;
      }

   } // namespace wasm
} // namespace bespoke
