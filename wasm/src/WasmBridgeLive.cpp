/**
 * BespokeSynth WASM - Live-instrument C ABI: pointer/touch, MIDI learn,
 * undo/redo, Scala tuning, and the Script module host hooks.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmBridge.h"
#include "BespokeWasm/WasmBridgeInternal.h"
#include "BespokeWasm/AudioAnalysis.h"
#include "BespokeWasm/AudioRemote.h"
#include "BespokeWasm/WasmInputRouter.h"
#include "BespokeWasm/ScriptNoteQueue.h"
#include "BespokeWasm/TuningTable.h"
#include "BespokeWasm/WasmRuntimeState.h"

#include <algorithm>
#include <cstdio>
#include <emscripten.h>
#include <sstream>
#include <string>

using namespace bespoke::wasm;

namespace
{
   std::string gLiveJsonBuffer;

   std::string escapeJson(const std::string& value)
   {
      std::string out;
      out.reserve(value.size() + 8);
      for (const char c : value)
      {
         switch (c)
         {
            case '"':
               out += "\\\"";
               break;
            case '\\':
               out += "\\\\";
               break;
            case '\n':
               out += "\\n";
               break;
            case '\r':
               out += "\\r";
               break;
            case '\t':
               out += "\\t";
               break;
            default:
               if (static_cast<unsigned char>(c) < 0x20)
               {
                  char escaped[8];
                  std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned char>(c));
                  out += escaped;
               }
               else
               {
                  out += c;
               }
         }
      }
      return out;
   }

   ModuleCanvas* canvas()
   {
      return runtimeState().canvas.get();
   }
}

extern "C" {

// --- Pointer / touch -------------------------------------------------------

EMSCRIPTEN_KEEPALIVE void bespoke_pointer_down(int pointerId, int x, int y, int button, int modifiers)
{
   gHostMouseDownCallCount.fetch_add(1, std::memory_order_relaxed);
   routePointerDown(pointerId, x, y, button, modifiers);
}

EMSCRIPTEN_KEEPALIVE void bespoke_pointer_move(int pointerId, int x, int y)
{
   routePointerMove(pointerId, x, y);
}

EMSCRIPTEN_KEEPALIVE void bespoke_pointer_up(int pointerId, int x, int y, int button)
{
   routePointerUp(pointerId, x, y, button);
}

EMSCRIPTEN_KEEPALIVE void bespoke_pointer_cancel(int pointerId)
{
   routePointerCancel(pointerId);
}

EMSCRIPTEN_KEEPALIVE void bespoke_long_press(int x, int y)
{
   if (auto* c = canvas(); c && runtimeState().viewMode == 0)
      c->onLongPress(static_cast<float>(x), static_cast<float>(y));
}

EMSCRIPTEN_KEEPALIVE void bespoke_canvas_zoom(float factor, float centerX, float centerY)
{
   if (auto* c = canvas(); c && factor > 0.0f)
      c->zoom(factor, centerX, centerY);
}

EMSCRIPTEN_KEEPALIVE void bespoke_canvas_pan(float dx, float dy)
{
   if (const ModuleCanvas* c = canvas())
   {
      const float scale = c->getScale();
      canvas()->pan(dx / scale, dy / scale);
   }
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_text_entry_active(void)
{
   auto* c = canvas();
   return c && runtimeState().viewMode == 0 && c->isSpawnMenuOpen() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_module_info_json(int moduleId)
{
   gLiveJsonBuffer = "{}";
   auto* c = canvas();
   Module* module = c ? c->getModule(moduleId) : nullptr;
   if (!module)
      return gLiveJsonBuffer.c_str();
   // Screen coordinates are CSS pixels relative to the canvas (same space as input).
   constexpr float kCanvasTop = 40.0f;
   const float scale = static_cast<const ModuleCanvas*>(c)->getScale();
   std::ostringstream out;
   out << "{\"id\":" << moduleId << ",\"type\":\"" << escapeJson(module->getType()) << "\""
       << ",\"x\":" << module->getX() << ",\"y\":" << module->getY()
       << ",\"width\":" << module->getWidth() << ",\"height\":" << module->getHeight()
       << ",\"screenX\":" << (module->getX() + c->getOffsetX()) * scale
       << ",\"screenY\":" << (module->getY() + c->getOffsetY()) * scale + kCanvasTop
       << ",\"scale\":" << scale << "}";
   gLiveJsonBuffer = out.str();
   return gLiveJsonBuffer.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_modules_json(void)
{
   gLiveJsonBuffer = "[]";
   auto* c = canvas();
   if (!c)
      return gLiveJsonBuffer.c_str();
   const auto snapshot = c->createStateSnapshot();
   std::ostringstream out;
   out << "[";
   for (size_t i = 0; i < snapshot.modules.size(); ++i)
   {
      const auto& module = snapshot.modules[i];
      out << (i ? "," : "") << "{\"id\":" << module.id << ",\"type\":\"" << escapeJson(module.type) << "\"}";
   }
   out << "]";
   gLiveJsonBuffer = out.str();
   return gLiveJsonBuffer.c_str();
}

// --- MIDI learn / channel -------------------------------------------------

/** -1 = omni (default); 0-15 = only that channel's notes reach the graph. */
EMSCRIPTEN_KEEPALIVE void bespoke_set_midi_note_channel(int channel)
{
   if (auto* c = canvas())
      c->setMidiNoteChannel(channel < 0 || channel > 15 ? -1 : channel);
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_midi_note_channel(void)
{
   auto* c = canvas();
   return c ? c->midiNoteChannel() : -1;
}

EMSCRIPTEN_KEEPALIVE int bespoke_midi_learn_arm(int moduleId, const char* controlName)
{
   auto* c = canvas();
   return c && c->armMidiLearn(moduleId, controlName ? controlName : "") ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_midi_learn_arm_at(int x, int y)
{
   auto* c = canvas();
   return c && c->armMidiLearnAt(static_cast<float>(x), static_cast<float>(y)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_midi_learn_cancel(void)
{
   if (auto* c = canvas())
      c->cancelMidiLearn();
}

EMSCRIPTEN_KEEPALIVE int bespoke_midi_learn_get_module(void)
{
   auto* c = canvas();
   return c ? c->midiLearnModuleId() : -1;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_midi_learn_get_control(void)
{
   auto* c = canvas();
   gLiveJsonBuffer = c ? c->midiLearnControl() : std::string();
   return gLiveJsonBuffer.c_str();
}

EMSCRIPTEN_KEEPALIVE int bespoke_midi_cc_unbind(int moduleId, const char* controlName)
{
   auto* c = canvas();
   return c && c->removeMidiCcBinding(moduleId, controlName ? controlName : "") ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_midi_cc_map_json(void)
{
   gLiveJsonBuffer = "[]";
   auto* c = canvas();
   if (!c)
      return gLiveJsonBuffer.c_str();
   const auto map = c->midiCcMap();
   std::ostringstream out;
   out << "[";
   for (size_t i = 0; i < map.size(); ++i)
   {
      const auto& binding = map[i];
      out << (i ? "," : "") << "{\"channel\":" << binding.channel << ",\"cc\":" << binding.cc
          << ",\"moduleId\":" << binding.moduleId << ",\"control\":\"" << escapeJson(binding.controlName)
          << "\",\"min\":" << binding.min << ",\"max\":" << binding.max << "}";
   }
   out << "]";
   gLiveJsonBuffer = out.str();
   return gLiveJsonBuffer.c_str();
}

// --- Undo / redo -----------------------------------------------------------

EMSCRIPTEN_KEEPALIVE int bespoke_undo(void)
{
   auto* c = canvas();
   return c && c->undo() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_redo(void)
{
   auto* c = canvas();
   return c && c->redo() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_undo_depth(void)
{
   auto* c = canvas();
   return c ? c->undoDepth() : 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_redo_depth(void)
{
   auto* c = canvas();
   return c ? c->redoDepth() : 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_clear_undo_history(void)
{
   if (auto* c = canvas())
      c->clearUndoHistory();
}

// --- Scala tuning ----------------------------------------------------------

EMSCRIPTEN_KEEPALIVE int bespoke_scale_set_tuning(const char* kind, const char* text, const char* name)
{
   auto* c = canvas();
   ScaleModule* scale = c ? c->getScale() : nullptr;
   if (!scale || !kind)
      return 0;
   const std::string key = kind;
   if (key != "scl" && key != "kbm")
      return 0;
   scale->setStringProperty(key + "Name", name ? name : "");
   // Parses the table and republishes so audio picks up the new frequencies.
   c->setModuleStringProperty(scale->getId(), key, text ? text : "");
   return scale->tuningError().empty() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_scale_get_tuning_error(void)
{
   auto* c = canvas();
   ScaleModule* scale = c ? c->getScale() : nullptr;
   gLiveJsonBuffer = scale ? scale->tuningError() : std::string();
   return gLiveJsonBuffer.c_str();
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_note_frequency(int pitch)
{
   auto* c = canvas();
   ScaleModule* scale = c ? c->getScale() : nullptr;
   return noteFrequency(scale ? scale->tuningTable().data() : nullptr, pitch);
}

// --- Graph notes for the MIDI output ---------------------------------------

/**
 * While on, the engine keeps a ring of the notes its note sources and processors emit
 * (sequencers, drum machine, script, arpeggiator, chorder...) for bespoke_drain_graph_notes.
 * The host turns it on only while a MIDI output port is selected.
 */
EMSCRIPTEN_KEEPALIVE void bespoke_set_graph_note_capture(int enabled)
{
   auto& state = runtimeState();
   if (state.audioGraphEngine)
      state.audioGraphEngine->setMidiOutCapture(enabled != 0);
}

/**
 * Pops up to `maxEvents` captured graph notes into `out`, one int each:
 * pitch | velocity(0-127) << 8 | channel << 16 | isNoteOn << 20. Returns the count.
 */
EMSCRIPTEN_KEEPALIVE int bespoke_drain_graph_notes(int* out, int maxEvents)
{
   auto& state = runtimeState();
   if (!out || maxEvents <= 0 || !state.audioGraphEngine)
      return 0;
   AudioGraphEngine::MidiOutNote notes[64];
   const int want = std::min(maxEvents, 64);
   const int got = state.audioGraphEngine->drainMidiOut(notes, want);
   for (int i = 0; i < got; ++i)
   {
      const int velocity = std::max(0, std::min(127, static_cast<int>(notes[i].velocity * 127.0f + 0.5f)));
      out[i] = (notes[i].pitch & 0x7F) | (velocity << 8) | ((notes[i].channel & 0xF) << 16) |
               ((notes[i].isNoteOn ? 1 : 0) << 20);
   }
   return got;
}

/**
 * 1 when a Note processor with no Note cable is listening to the performance (QWERTY / MIDI in),
 * so the processor's output - not the raw key - is what reaches MIDI out.
 */
EMSCRIPTEN_KEEPALIVE int bespoke_performance_notes_processed(void)
{
   auto& state = runtimeState();
   if (!state.canvas)
      return 0;
   const auto snapshot = state.canvas->getAudioGraphSnapshot();
   return snapshot && snapshot->processPlan.performanceNotesProcessed ? 1 : 0;
}

/** Saved modules whose type this build doesn't have (kept as labelled placeholders). */
EMSCRIPTEN_KEEPALIVE int bespoke_get_placeholder_count(void)
{
   auto* c = canvas();
   return c ? c->placeholderCount() : 0;
}

// --- Script module host ----------------------------------------------------

EMSCRIPTEN_KEEPALIVE void bespoke_set_scripting_enabled(int enabled)
{
   if (auto* c = canvas())
      c->setScriptingEnabled(enabled != 0);
}

/**
 * Queue a note from a script. Uncabled script modules play like a MIDI keyboard.
 * Returns 0 when rejected, 1 when queued on the script module's own Note cables (the engine
 * reports those to MIDI out), 2 when played on the performance path (the host echoes them).
 */
EMSCRIPTEN_KEEPALIVE int bespoke_script_note(int moduleId, int pitch, float velocity, int isNoteOn)
{
   auto& state = runtimeState();
   auto* c = canvas();
   if (!c || !state.audioGraphEngine || pitch < 0 || pitch > 127)
      return 0;
   Module* module = c->getModule(moduleId);
   if (!module || module->getType() != "script")
      return 0;
   const float clamped = std::max(0.0f, std::min(1.0f, velocity));
   if (!c->hasOutgoingConnection(moduleId))
   {
      queuePerformanceNote({ pitch, isNoteOn ? clamped : 0.0f, isNoteOn != 0, emscripten_get_now() / 1000.0, 0 });
      return 2; // performance path: the engine does not report these to MIDI out
   }
   WasmNoteEvent note;
   note.pitch = pitch;
   note.velocity = isNoteOn ? clamped : 0.0f;
   note.isNoteOn = isNoteOn != 0;
   return queueScriptNote(moduleId, note) ? 1 : 0;
}

/** Script automation: applies immediately and is not recorded for undo. */
EMSCRIPTEN_KEEPALIVE int bespoke_script_set_control(int moduleId, const char* controlName, float value)
{
   auto* c = canvas();
   if (!c || !controlName)
      return 0;
   return c->setModuleControlValue(moduleId, controlName, value, false) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_script_stopped(int moduleId)
{
   clearScriptNoteQueue(moduleId);
}

EMSCRIPTEN_KEEPALIVE double bespoke_get_transport_beat(void)
{
   return playingTransportBeat();
}

/** Copies the most recent output samples (analyzer ring) for diagnostics/tests. */
EMSCRIPTEN_KEEPALIVE int bespoke_copy_output_waveform(float* destination, int count)
{
   if (!destination || count <= 0)
      return 0;
   count = std::min(count, AudioAnalysis::kRingSize);
   AudioAnalysis::copyLatest(destination, count);
   return count;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_analyzer_cpu_fft_count(void)
{
   return static_cast<int>(AudioAnalysis::cpuSpectrumCount());
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_analyzer_gpu_fft_count(void)
{
   return static_cast<int>(AudioAnalysis::gpuSpectrumCount());
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_transport_playing(void)
{
   auto* c = canvas();
   return c && c->isTransportPlaying() ? 1 : 0;
}

} // extern "C"
