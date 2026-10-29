/**
 * BespokeSynth WASM - UI module side of the AudioWorklet engine
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioRemote.h"
#include "BespokeWasm/AudioWorkletEngine.h"
#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/ScriptNoteQueue.h"
#include "BespokeWasm/WasmRuntimeState.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <emscripten.h>

// The host installs Module.bespokeAudioRemote while the worklet engine is live
// (src/audio/workletEngineBackend.ts); without it these are no-ops. Host-defined
// names use bracket access: Closure (Release) renames dotted properties.
// clang-format off
EM_JS(int, bespoke_js_remote_audio_event, (int type, int a, int b, int c), {
   var remote = Module['bespokeAudioRemote'];
   if (!remote) return 0;
   return remote['event'](type, a, b, c) === false ? 0 : 1;
});

EM_JS(void, bespoke_js_remote_audio_graph, (int reset), {
   var remote = Module['bespokeAudioRemote'];
   if (remote) remote['graphChanged'](reset !== 0);
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

         bool sendEvent(AudioEngineEventType type, int a, int b, int c)
         {
            return bespoke_js_remote_audio_event(static_cast<int>(type), a, b, c) != 0;
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
            int packed = (note.pitch & 0x7f) | ((note.channel & 0x0f) << 8) | ((note.isNoteOn ? 1 : 0) << 12);
            int frame = 0;
            const float rate = state.externalSampleRate;
            if (note.timestamp > 0.0 && rate > 0.0f)
            {
               // The record carries the low 32 bits of the start frame; the engine unwraps them.
               packed |= kAudioEngineNoteTimed;
               frame = static_cast<int>(static_cast<uint32_t>(std::llround(note.timestamp * rate)));
            }
            sendEvent(AudioEngineEventType::Note, packed, floatBits(note.velocity), frame);
            return;
         }
         if (!state.audioGraphEngine)
            return;
         state.audioGraphEngine->queueNote(note);
         // SDL and the main-thread ring stop calling processBlock with the transport, on this
         // thread; fold the note now so a long stopped session cannot fill the ring.
         if (!state.canvas || !state.canvas->isTransportPlaying())
            state.audioGraphEngine->absorbQueuedNotesWhileStopped();
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

      bool queueParamChange(const ParamChange& change)
      {
         auto& state = runtimeState();
         if (state.remoteAudioActive)
         {
            if (change.moduleId < -1 || change.moduleId >= 0xFFFFF || change.paramId >= 0x1000)
               return false;
            const int packed = ((change.moduleId + 1) & 0xFFFFF) | (static_cast<int>(change.paramId) << 20);
            return sendEvent(AudioEngineEventType::ParamChange, packed, floatBits(change.value),
                             static_cast<int>(change.seq));
         }
         if (!state.audioGraphEngine || !state.audioGraphEngine->queueParamChange(change))
            return false;
         // SDL and the main-thread ring stop calling processBlock with the transport; apply now.
         if (!state.canvas || !state.canvas->isTransportPlaying())
         {
            if (const auto graph = state.canvas ? state.canvas->getAudioGraphSnapshot() : nullptr)
               state.audioGraphEngine->applyQueuedParams(*graph);
         }
         return true;
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
