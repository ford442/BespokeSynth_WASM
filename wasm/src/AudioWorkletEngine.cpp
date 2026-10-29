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
#include "BespokeWasm/AudioRtGuard.h"
#include "BespokeWasm/InputAudioBus.h"
#include "BespokeWasm/LooperArena.h"
#include "BespokeWasm/ResourceLifetime.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/ScriptNoteQueue.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <emscripten.h>

using namespace bespoke::wasm;

namespace
{
   constexpr int kAudioHealthBackendWorkletWasm = 3;
   static_assert(kStatusSlotCount - kStatusBlockTimeHistogram == kAudioBlockTimeBuckets,
                 "status block carries the whole block-time histogram");

   /** Notes a Script worker sent straight to this engine and still holds (released on ScriptClear). */
   struct DirectScript
   {
      int moduleId = -1;
      /** Per pitch, as bespoke_script_note returns: 0 not held, 1 on the module's own cables, 2 played like a keyboard. */
      std::array<uint8_t, 128> route{};
      int held = 0;
      /** Latest start frame it asked for; its release note-offs go no earlier. */
      int64_t lastFrame = 0;
   };

   struct WorkletEngine
   {
      std::unique_ptr<AudioGraphEngine> engine;
      /** The graph processBlock reads; replaced whole when a snapshot is applied. */
      std::unique_ptr<AudioGraphSnapshot> graph;
      float sampleRate = 48000.0f;
      int maxBlock = 0;
      uint64_t snapshotCount = 0;
      uint64_t directScriptNotes = 0;
      /** AudioContext frame of the next block (currentFrame after the last one). */
      int64_t nextFrame = 0;
      AudioThreadScope scope = AudioThreadScope::Unknown;
      std::array<DirectScript, kMaxScriptQueues> scripts{};
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

   /** The frame whose low 32 bits are `low`, nearest the next block (records carry 32 bits). */
   int64_t unwrapFrame(const WorkletEngine& w, int32_t low)
   {
      const auto reference = static_cast<uint32_t>(static_cast<uint64_t>(w.nextFrame));
      const auto delta = static_cast<int32_t>(static_cast<uint32_t>(low) - reference);
      return w.nextFrame + delta;
   }

   double frameSeconds(const WorkletEngine& w, int64_t frame)
   {
      return frame > 0 ? static_cast<double>(frame) / static_cast<double>(w.sampleRate) : 0.0;
   }

   bool hasNoteCables(const AudioGraphSnapshot* graph, int moduleId)
   {
      if (!graph)
         return false;
      return std::any_of(graph->connections.begin(), graph->connections.end(),
                         [moduleId](const AudioGraphConnection& c)
                         {
                            return c.sourceModuleId == moduleId;
                         });
   }

   DirectScript* directScript(WorkletEngine& w, int moduleId, bool create)
   {
      DirectScript* free = nullptr;
      for (auto& script : w.scripts)
      {
         if (script.moduleId == moduleId)
            return &script;
         if (!free && script.moduleId < 0)
            free = &script;
      }
      if (!create || !free)
         return nullptr;
      *free = DirectScript{};
      free->moduleId = moduleId;
      return free;
   }

   /** Route 1 goes to the module's cables; route 2 plays like a keyboard (echoed to MIDI out). */
   void queueRouted(WorkletEngine& w, int moduleId, int route, int pitch, float velocity, bool on, double timestamp)
   {
      AudioGraphEngine::NoteMessage note;
      note.pitch = pitch;
      note.velocity = on ? velocity : 0.0f;
      note.isNoteOn = on;
      note.timestamp = timestamp;
      note.scriptModuleId = route == 1 ? moduleId : -1;
      note.echoMidiOut = route == 2;
      w.engine->queueNote(note);
   }

   /** Like bespoke_script_note, decided here: the engine's graph knows the module's cables. */
   void applyDirectScriptNote(WorkletEngine& w, int moduleId, int pitch, float velocity, bool on, int64_t frame)
   {
      const int cabledRoute = hasNoteCables(w.graph.get(), moduleId) ? 1 : 2;
      DirectScript* script = directScript(w, moduleId, on);
      int route = cabledRoute;
      if (script)
      {
         uint8_t& held = script->route[static_cast<size_t>(pitch)];
         // A note-off follows its note-on, even if the module was cabled in between.
         if (!on && held != 0)
            route = held;
         if (on && held == 0)
            ++script->held;
         else if (!on && held != 0)
            --script->held;
         held = on ? static_cast<uint8_t>(route) : 0;
         script->lastFrame = std::max(script->lastFrame, frame);
         if (script->held == 0)
            script->moduleId = -1;
      }
      ++w.directScriptNotes;
      queueRouted(w, moduleId, route, pitch, velocity, on, frameSeconds(w, frame));
   }

   /** The Script module stopped: everything it holds goes off after every note it already sent. */
   void releaseDirectScript(WorkletEngine& w, int moduleId)
   {
      DirectScript* script = directScript(w, moduleId, false);
      if (!script)
         return;
      const double at = script->lastFrame > w.nextFrame ? frameSeconds(w, script->lastFrame) : 0.0;
      for (int pitch = 0; pitch < 128; ++pitch)
      {
         const uint8_t route = script->route[static_cast<size_t>(pitch)];
         if (route != 0)
            queueRouted(w, moduleId, route, pitch, 0.0f, false, at);
      }
      *script = DirectScript{};
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
   w.nextFrame = 0;
   w.directScriptNotes = 0;
   w.scripts = {};
   w.scope = currentAudioThreadScope();
   SampleStore::instance().setDeviceSampleRate(static_cast<int>(w.sampleRate));
   audioHealthReset();
   audioHealthSetBackend(kAudioHealthBackendWorkletWasm);
   resetAudioRtViolations();
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
   // Recordings live in this heap, so its arenas follow this heap's snapshots. Samples are
   // copies of the UI module's and only go when it evicts them (bespoke_audio_evict_sample).
   const WasmResourceRefs refs = collectSnapshotResources(*next);
   LooperArenaPool::instance().retainOnly(refs.looperModuleIds, next->generation);
   w.graph = std::move(next);
   ++w.snapshotCount;
   // The next block processes w.graph, so nothing older can reach the engine.
   collectUnusedResources(w.graph->generation);
   return 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_audio_last_error(void)
{
   return worklet().lastError.c_str();
}

/**
 * Takes a copy of a sample the UI module decoded, under the UI module's id and generation (so
 * SamplerParams::handle resolves here too). `planar` holds `channels` planes of `frames` floats,
 * already at the device rate. Returns 0 on success.
 */
EMSCRIPTEN_KEEPALIVE int bespoke_audio_adopt_sample(int id,
                                                    int channels,
                                                    int frames,
                                                    int sampleRate,
                                                    const float* planar,
                                                    int generation)
{
   if (id < 0 || channels < 1 || channels > 2 || frames <= 0 || !planar)
      return -1;
   std::vector<float> samples(planar, planar + static_cast<size_t>(channels) * static_cast<size_t>(frames));
   SampleStore::instance().adopt(
   id, std::make_shared<SampleBuffer>(std::move(samples), channels, sampleRate, std::string(), std::string()),
   static_cast<uint32_t>(generation));
   return 0;
}

/**
 * The UI module evicted sample `id`. The host posts this after the snapshot that stopped naming
 * it, and messages run between blocks, so the engine no longer reads it.
 */
EMSCRIPTEN_KEEPALIVE void bespoke_audio_evict_sample(int id)
{
   SampleStore::instance().evict(id);
}

/**
 * Applies `count` event records (AudioEngineEventType, kAudioEngineEventWords int32 each). Start
 * frames are read against the next block's frame (the last bespoke_audio_process).
 */
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
            if ((record[1] & kAudioEngineNoteTimed) != 0)
               note.timestamp = frameSeconds(w, unwrapFrame(w, record[3]));
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
         case AudioEngineEventType::ScriptDirectNote:
         {
            if (!w.engine)
               break;
            const auto packed = static_cast<uint32_t>(record[1]);
            const int moduleId = static_cast<int>(packed & 0xFFFFF) - 1;
            if (moduleId < 0)
               break;
            const int pitch = static_cast<int>((packed >> 20) & 0x7f);
            const bool on = ((packed >> 27) & 1) != 0;
            const float velocity = std::max(0.0f, std::min(1.0f, floatFromBits(record[2])));
            const int64_t frame = (packed & kAudioEngineScriptDirectTimed) != 0 ? unwrapFrame(w, record[3]) : 0;
            applyDirectScriptNote(w, moduleId, pitch, velocity, on, frame);
            break;
         }
         case AudioEngineEventType::ScriptClear:
            clearScriptNotes(record[1]);
            if (w.engine)
               releaseDirectScript(w, record[1]);
            break;
         case AudioEngineEventType::ParamChange:
         {
            if (!w.engine)
               break;
            ParamChange change;
            change.moduleId = (record[1] & 0xFFFFF) - 1;
            change.paramId = static_cast<ParamId>((static_cast<uint32_t>(record[1]) >> 20) & 0xFFF);
            change.value = floatFromBits(record[2]);
            change.seq = static_cast<uint32_t>(record[3]);
            // Drops are counted in the engine's stats; the UI side republishes on its next edit.
            (void)w.engine->queueParamChange(change);
            break;
         }
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

/**
 * Renders one block into the planar `left`/`right` buffers (`frames` <= the init block size).
 * `blockFrame` is the AudioContext frame the block starts on (the worklet's currentFrame), the
 * clock timed notes are placed on; negative renders every note at the block start.
 */
EMSCRIPTEN_KEEPALIVE void bespoke_audio_process(float* left, float* right, int frames, double blockFrame)
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
#if defined(BESPOKE_WASM_RT_COUNT)
   // Debug: this module exists to keep processBlock off the main thread.
   if (w.scope != AudioThreadScope::AudioWorklet)
      std::abort();
#endif

   const auto started = std::chrono::steady_clock::now();
   double intervalSeconds = 0.0;
   if (w.hasLastCallback)
      intervalSeconds = std::chrono::duration<double>(started - w.lastCallback).count();
   w.lastCallback = started;
   w.hasLastCallback = true;

   const bool clocked = blockFrame >= 0.0;
   const double hostTime = clocked ? blockFrame / static_cast<double>(w.sampleRate) : -1.0;
   float* channels[2] = { left, right };
   if (w.graph)
   {
      w.engine->processBlock(*w.graph, channels, 2, frames, w.sampleRate, hostTime);
   }
   else
   {
      std::memset(left, 0, static_cast<size_t>(frames) * sizeof(float));
      std::memset(right, 0, static_cast<size_t>(frames) * sizeof(float));
   }
   if (clocked)
      w.nextFrame = static_cast<int64_t>(blockFrame) + frames;

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
   if (w.engine)
   {
      const AudioGraphEngine::Stats stats = w.engine->stats();
      status[kStatusBpm] = w.engine->transportBpm();
      status[kStatusPlaying] = w.engine->transportPlaying() ? 1.0 : 0.0;
      status[kStatusTimedNotes] = static_cast<double>(stats.timedNotes);
      status[kStatusLateNotes] = static_cast<double>(stats.lateNotes);
      status[kStatusSplitBlocks] = static_cast<double>(stats.splitBlocks);
   }
   status[kStatusDirectScriptNotes] = static_cast<double>(w.directScriptNotes);
   const AudioRtViolations rt = audioRtViolations();
   status[kStatusRtAllocations] = static_cast<double>(rt.allocations);
   status[kStatusRtFrees] = static_cast<double>(rt.frees);
   status[kStatusRtLocks] = static_cast<double>(rt.locks);
   status[kStatusRtCountsAllocations] = rt.countsAllocations ? 1.0 : 0.0;
   status[kStatusEngineScope] = static_cast<double>(w.scope);
   for (int i = 0; i < kAudioBlockTimeBuckets; ++i)
      status[kStatusBlockTimeHistogram + i] = static_cast<double>(health.blockTimeHistogram[i]);
   std::memcpy(out, status, static_cast<size_t>(std::min(count, static_cast<int>(kStatusSlotCount))) * sizeof(double));
}

} // extern "C"
