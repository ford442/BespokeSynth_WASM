/**
 * BespokeSynth WASM - AudioWorklet engine C ABI (BespokeSynthWASM_audio only)
 *
 * Everything here runs on the AudioWorklet thread. bespoke_audio_process is the
 * real-time path; the other calls arrive between render quanta from the
 * processor's message handler (snapshots, samples, resets) and may allocate.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioWorkletEngine.h"
#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/AudioGraphSnapshotCodec.h"
#include "BespokeWasm/AudioHealth.h"
#include "BespokeWasm/InputAudioBus.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/ScriptNoteQueue.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <emscripten.h>

using namespace bespoke::wasm;

namespace
{
   constexpr int kAudioHealthBackendWorkletWasm = 3;

   struct WorkletEngine
   {
      std::unique_ptr<AudioGraphEngine> engine;
      /** The graph processBlock reads; replaced whole when a snapshot is applied. */
      std::unique_ptr<AudioGraphSnapshot> graph;
      float sampleRate = 48000.0f;
      int maxBlock = 0;
      uint64_t snapshotCount = 0;
      std::string lastError;
      std::chrono::steady_clock::time_point lastCallback{};
      bool hasLastCallback = false;
   };

   WorkletEngine& worklet()
   {
      static WorkletEngine state;
      return state;
   }

   float floatFromBits(int32_t bits)
   {
      float value = 0.0f;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
   }
}

extern "C" {

/** Wire ABI of this build; the host refuses to pair it with a UI module that differs. */
EMSCRIPTEN_KEEPALIVE int bespoke_audio_wire_abi(void)
{
   return static_cast<int>(audioGraphWireAbi());
}

/** Sizes the engine for blocks up to `maxBlock` frames. Returns 0 on success. */
EMSCRIPTEN_KEEPALIVE int bespoke_audio_init(int sampleRate, int maxBlock)
{
   auto& w = worklet();
   w.sampleRate = sampleRate > 0 ? static_cast<float>(sampleRate) : 48000.0f;
   w.maxBlock = std::max(128, std::min(maxBlock, AudioGraphEngine::kMaxBlockSize));
   w.engine = std::make_unique<AudioGraphEngine>();
   // Same arena sizing as the UI module's engine (bespoke_init).
   w.engine->prepareForBlock(AudioGraphEngine::kMaxBlockSize, 128, 32);
   SampleStore::instance().setDeviceSampleRate(static_cast<int>(w.sampleRate));
   audioHealthReset();
   audioHealthSetBackend(kAudioHealthBackendWorkletWasm);
   return 0;
}

/**
 * Applies a snapshot from the UI module (encodeAudioGraphSnapshot bytes): decode, size runtime
 * state for it, then swap it in. `reset` (a patch was loaded) first restarts the clock and drops
 * every module's runtime state, like the UI module's resetAudioGraphEngineForPatch. Returns 0, or
 * -1 with bespoke_audio_last_error set; a rejected snapshot leaves the previous graph playing.
 */
EMSCRIPTEN_KEEPALIVE int bespoke_audio_apply_snapshot(const unsigned char* bytes, int length, int reset)
{
   auto& w = worklet();
   if (!w.engine || length <= 0)
   {
      w.lastError = "engine not initialized";
      return -1;
   }
   auto next = std::make_unique<AudioGraphSnapshot>();
   if (!decodeAudioGraphSnapshot(bytes, static_cast<size_t>(length), *next, w.lastError))
      return -1;
   if (reset)
      w.engine->resetClock();
   w.engine->prepareRuntimeStates(*next);
   w.graph = std::move(next);
   ++w.snapshotCount;
   return 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_audio_last_error(void)
{
   return worklet().lastError.c_str();
}

/**
 * Takes a copy of a sample the UI module decoded, under the UI module's id. `planar` holds
 * `channels` planes of `frames` floats, already at the device rate. Returns 0 on success.
 */
EMSCRIPTEN_KEEPALIVE int bespoke_audio_adopt_sample(int id, int channels, int frames, int sampleRate, const float* planar)
{
   if (id < 0 || channels < 1 || channels > 2 || frames <= 0 || !planar)
      return -1;
   std::vector<float> samples(planar, planar + static_cast<size_t>(channels) * static_cast<size_t>(frames));
   SampleStore::instance().adopt(
   id, std::make_shared<SampleBuffer>(std::move(samples), channels, sampleRate, std::string(), std::string()));
   return 0;
}

/** Applies `count` event records (AudioEngineEventType, kAudioEngineEventWords int32 each). */
EMSCRIPTEN_KEEPALIVE void bespoke_audio_apply_events(const int* words, int count)
{
   auto& w = worklet();
   if (!words || count <= 0)
      return;
   for (int i = 0; i < count; ++i)
   {
      const int* record = words + static_cast<size_t>(i) * kAudioEngineEventWords;
      switch (static_cast<AudioEngineEventType>(record[0]))
      {
         case AudioEngineEventType::Note:
         {
            if (!w.engine)
               break;
            AudioGraphEngine::NoteMessage note;
            note.pitch = record[1] & 0x7f;
            note.channel = (record[1] >> 8) & 0x0f;
            note.isNoteOn = ((record[1] >> 12) & 1) != 0;
            note.velocity = note.isNoteOn ? std::max(0.0f, std::min(1.0f, floatFromBits(record[2]))) : 0.0f;
            w.engine->queueNote(note);
            break;
         }
         case AudioEngineEventType::ScriptNote:
         {
            WasmNoteEvent note;
            note.pitch = record[2] & 0x7f;
            note.isNoteOn = ((record[2] >> 8) & 1) != 0;
            note.velocity = note.isNoteOn ? std::max(0.0f, std::min(1.0f, floatFromBits(record[3]))) : 0.0f;
            pushScriptNote(record[1], note);
            break;
         }
         case AudioEngineEventType::ScriptClear:
            clearScriptNotes(record[1]);
            break;
         default:
            break;
      }
   }
}

/** Microphone frames (mono) for the Input module. */
EMSCRIPTEN_KEEPALIVE void bespoke_audio_push_input(const float* frames, int count)
{
   InputAudioBus::instance().push(frames, count);
}

EMSCRIPTEN_KEEPALIVE void bespoke_audio_set_input_active(int active)
{
   InputAudioBus::instance().setCaptureActive(active != 0);
}

EMSCRIPTEN_KEEPALIVE void bespoke_audio_set_midi_out_capture(int enabled)
{
   if (auto* engine = worklet().engine.get())
      engine->setMidiOutCapture(enabled != 0);
}

/** Renders one block into the planar `left`/`right` buffers (`frames` <= the init block size). */
EMSCRIPTEN_KEEPALIVE void bespoke_audio_process(float* left, float* right, int frames)
{
   auto& w = worklet();
   if (!left || !right || frames <= 0)
      return;
   if (!w.engine || frames > w.maxBlock)
   {
      std::memset(left, 0, static_cast<size_t>(std::max(frames, 0)) * sizeof(float));
      std::memset(right, 0, static_cast<size_t>(std::max(frames, 0)) * sizeof(float));
      return;
   }

   const auto started = std::chrono::steady_clock::now();
   double intervalSeconds = 0.0;
   if (w.hasLastCallback)
      intervalSeconds = std::chrono::duration<double>(started - w.lastCallback).count();
   w.lastCallback = started;
   w.hasLastCallback = true;

   float* channels[2] = { left, right };
   if (w.graph)
   {
      w.engine->processBlock(*w.graph, channels, 2, frames, w.sampleRate);
   }
   else
   {
      std::memset(left, 0, static_cast<size_t>(frames) * sizeof(float));
      std::memset(right, 0, static_cast<size_t>(frames) * sizeof(float));
   }

   const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
   audioHealthOnCallback(elapsed, static_cast<double>(frames) / w.sampleRate, intervalSeconds);
   audioHealthSetNoteDropCount(w.engine->noteDropCount());
}

/**
 * Pops up to `maxEvents` notes the graph played for the MIDI output, packed like the UI
 * module's bespoke_drain_graph_notes: pitch | velocity << 8 | channel << 16 | isNoteOn << 20.
 */
EMSCRIPTEN_KEEPALIVE int bespoke_audio_drain_midi_out(int* out, int maxEvents)
{
   auto& w = worklet();
   if (!out || maxEvents <= 0 || !w.engine)
      return 0;
   AudioGraphEngine::MidiOutNote notes[64];
   const int got = w.engine->drainMidiOut(notes, std::min(maxEvents, 64));
   for (int i = 0; i < got; ++i)
   {
      const int velocity = std::max(0, std::min(127, static_cast<int>(notes[i].velocity * 127.0f + 0.5f)));
      out[i] = (notes[i].pitch & 0x7F) | (velocity << 8) | ((notes[i].channel & 0xF) << 16) |
               ((notes[i].isNoteOn ? 1 : 0) << 20);
   }
   return got;
}

/**
 * Fills `count` AudioEngineStatusSlot values. Peaks are taken (reset) by this call, so the
 * host folds them into its own running maximum.
 */
EMSCRIPTEN_KEEPALIVE void bespoke_audio_read_status(double* out, int count)
{
   auto& w = worklet();
   if (!out || count <= 0)
      return;
   double status[kStatusSlotCount]{};
   const AudioHealthSnapshot health = audioHealthSnapshot();
   status[kStatusBeat] = w.engine ? w.engine->beatPosition() : 0.0;
   status[kStatusCallbackCount] = static_cast<double>(health.callbackCount);
   status[kStatusUnderrunCount] = static_cast<double>(health.underrunCount);
   status[kStatusLastProcessMs] = health.lastProcessTimeMs;
   status[kStatusMaxProcessMs] = health.maxProcessTimeMs;
   status[kStatusCpuLoad] = health.cpuLoad;
   status[kStatusLastIntervalMs] = health.lastCallbackIntervalMs;
   status[kStatusNoteDropCount] = static_cast<double>(health.noteDropCount);
   status[kStatusHeapBytes] = static_cast<double>(health.heapBytes);
   status[kStatusMidiOutDropCount] = w.engine ? static_cast<double>(w.engine->midiOutDropCount()) : 0.0;
   status[kStatusOutputPeakL] = w.engine ? w.engine->takeOutputPeak(0) : 0.0f;
   status[kStatusOutputPeakR] = w.engine ? w.engine->takeOutputPeak(1) : 0.0f;
   status[kStatusInputPeak] = InputAudioBus::instance().takePeak();
   status[kStatusSnapshotCount] = static_cast<double>(w.snapshotCount);
   std::memcpy(out, status, static_cast<size_t>(std::min(count, static_cast<int>(kStatusSlotCount))) * sizeof(double));
}

} // extern "C"
