/**
 * BespokeSynth WASM - Audio / transport / offline C ABI
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmBridge.h"
#include "BespokeWasm/WasmBridgeInternal.h"
#include "BespokeWasm/AudioAnalysis.h"
#include "BespokeWasm/AudioHealth.h"
#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/AudioGraphSnapshotCodec.h"
#include "BespokeWasm/AudioRemote.h"
#include "BespokeWasm/WasmAudioEngine.h"
#include "BespokeWasm/InputAudioBus.h"
#include "BespokeWasm/OfflineRender.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/WasmRuntimeState.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <emscripten.h>

using namespace bespoke::wasm;

namespace
{
   std::vector<uint8_t> gOfflineWav;
   int gOfflineBits = 32;
   std::vector<uint8_t> gEncodedGraph;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void bespoke_process_audio(void)
{
}

EMSCRIPTEN_KEEPALIVE void bespoke_process_audio_block(float* output, int frames)
{
   processWasmAudioInterleaved(output, frames);
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_sample_rate(int sampleRate)
{
   printf("BespokeSynth WASM: Sample rate change requested: %d\n", sampleRate);
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_buffer_size(int bufferSize)
{
   printf("BespokeSynth WASM: Buffer size change requested: %d\n", bufferSize);
   auto& state = runtimeState();
   if (state.audioGraphEngine)
      state.audioGraphEngine->prepareForBlock(std::max(bufferSize, 2048), 128, 32);
   prepareWasmAudioScratch(std::max(bufferSize, 2048));
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_sample_rate(void)
{
   auto& state = runtimeState();
   return state.audioBackend ? state.audioBackend->getSampleRate() : 44100;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_buffer_size(void)
{
   auto& state = runtimeState();
   return state.audioBackend ? state.audioBackend->getBufferSize() : 512;
}

EMSCRIPTEN_KEEPALIVE void bespoke_play(void)
{
   auto& state = runtimeState();
   if (state.audioBackend && !state.externalAudioActive)
      state.audioBackend->start();
   if (state.canvas)
      state.canvas->setTransportPlaying(true);
}

EMSCRIPTEN_KEEPALIVE void bespoke_stop(void)
{
   auto& state = runtimeState();
   if (state.audioBackend && !state.externalAudioActive)
      state.audioBackend->stop();
   if (state.canvas)
      state.canvas->setTransportPlaying(false);
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_tempo(float bpm)
{
   if (runtimeState().canvas)
      runtimeState().canvas->setTransportBPM(bpm);
   printf("BespokeSynth WASM: Set tempo to %.1f BPM\n", bpm);
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_tempo(void)
{
   if (runtimeState().canvas)
      return runtimeState().canvas->getTransportBPM();
   return 120.0f;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_version(void)
{
   return kWasmVersion;
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_cpu_load(void)
{
   return wasmAudioCpuLoad();
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_external_audio(int enabled)
{
   auto& state = runtimeState();
   state.externalAudioActive = enabled != 0;
   syncSampleStoreRate();
   if (state.externalAudioActive && state.audioBackend && state.audioBackend->isRunning())
      state.audioBackend->stop();
   printf("BespokeSynth WASM: external audio %s\n",
          state.externalAudioActive ? "enabled" : "disabled");
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_external_audio(void)
{
   return runtimeState().externalAudioActive ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_external_sample_rate(float sampleRate)
{
   if (sampleRate > 0.0f)
   {
      runtimeState().externalSampleRate = sampleRate;
      syncSampleStoreRate();
   }
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_audio_backend_id(int backend)
{
   audioHealthSetBackend(backend);
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_audio_health_json(void)
{
   return audioHealthJson();
}

EMSCRIPTEN_KEEPALIVE void bespoke_reset_audio_health(void)
{
   audioHealthReset();
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_audio_queue_stats(int depthFrames, int capacityFrames, double externalUnderruns)
{
   audioHealthSetQueueStats(depthFrames, capacityFrames,
                            static_cast<uint64_t>(externalUnderruns < 0.0 ? 0.0 : externalUnderruns));
}

EMSCRIPTEN_KEEPALIVE void bespoke_push_input_audio(const float* frames, int framesCount)
{
   InputAudioBus::instance().push(frames, framesCount);
}

// The host reports whether the microphone is open so the Input module can say so.
EMSCRIPTEN_KEEPALIVE void bespoke_set_input_capture_active(int active)
{
   InputAudioBus::instance().setCaptureActive(active != 0);
}

// --- AudioWorklet engine (Option A): this module edits, a second instance plays ---

/**
 * 1: the AudioWorklet engine plays the graph from now on, so this module's engine stops
 * sizing state and refuses to render; notes, script notes and graph changes go to the host
 * (Module.bespokeAudioRemote). 0: back to local rendering (worklet engine failed or closed).
 */
EMSCRIPTEN_KEEPALIVE void bespoke_set_audio_remote(int enabled)
{
   auto& state = runtimeState();
   const bool remote = enabled != 0;
   if (remote == state.remoteAudioActive)
      return;
   state.remoteAudioActive = remote;
   state.remoteBeat = 0.0;
   state.remoteOutputPeak[0] = state.remoteOutputPeak[1] = 0.0f;
   if (!state.audioGraphEngine)
      return;
   // Notes queued before the worklet took over (a chord held before Play) are its to play.
   static AudioGraphEngine::NoteMessage pending[AudioGraphEngine::kMaxNoteRingCapacity];
   const int pendingCount =
   remote ? state.audioGraphEngine->takeQueuedNotes(pending, AudioGraphEngine::kMaxNoteRingCapacity) : 0;
   // Free the idle engine's delay lines and loops; re-size them if we take playback back.
   state.audioGraphEngine->resetClock();
   for (int i = 0; i < pendingCount; ++i)
      queuePerformanceNote(pending[i]);
   if (!remote && state.canvas)
   {
      if (const auto graph = state.canvas->getAudioGraphSnapshot())
         state.audioGraphEngine->prepareRuntimeStates(*graph);
   }
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_audio_remote(void)
{
   return runtimeState().remoteAudioActive ? 1 : 0;
}

/** Must equal the worklet module's bespoke_audio_wire_abi for snapshots to cross. */
EMSCRIPTEN_KEEPALIVE int bespoke_get_audio_wire_abi(void)
{
   return static_cast<int>(audioGraphWireAbi());
}

/**
 * Encodes the latest published graph for the worklet engine. The bytes stay valid until the
 * next call; null (and 0 length) before the canvas exists.
 */
EMSCRIPTEN_KEEPALIVE const unsigned char* bespoke_encode_audio_graph(int* outLength)
{
   auto& state = runtimeState();
   gEncodedGraph.clear();
   if (state.canvas)
   {
      if (const auto graph = state.canvas->getAudioGraphSnapshot())
         encodeAudioGraphSnapshot(*graph, gEncodedGraph);
   }
   if (outLength)
      *outLength = static_cast<int>(gEncodedGraph.size());
   return gEncodedGraph.empty() ? nullptr : gEncodedGraph.data();
}

/** Samples decoded so far; ids run 0..count-1 and are never reused. */
EMSCRIPTEN_KEEPALIVE int bespoke_get_sample_count(void)
{
   return SampleStore::instance().count();
}

/**
 * Describes sample `id` for copying into the worklet engine: out[0] channels, out[1] frames,
 * out[2] sample rate, out[3] pointer to `channels` back-to-back planes. Returns 0 when absent.
 */
EMSCRIPTEN_KEEPALIVE int bespoke_get_sample_info(int id, int* out)
{
   const SampleBuffer* sample = SampleStore::instance().findById(id);
   if (!sample || !out)
      return 0;
   out[0] = sample->channelCount();
   out[1] = sample->frameCount();
   out[2] = sample->sampleRate();
   out[3] = static_cast<int>(reinterpret_cast<uintptr_t>(sample->channelData(0)));
   return 1;
}

/** Host, once per frame in remote mode: what the worklet engine last reported. Peaks accumulate. */
EMSCRIPTEN_KEEPALIVE void bespoke_set_remote_audio_status(double beat, float outputPeakL, float outputPeakR,
                                                          float inputPeak)
{
   auto& state = runtimeState();
   state.remoteBeat = beat;
   state.remoteOutputPeak[0] = std::max(state.remoteOutputPeak[0], outputPeakL);
   state.remoteOutputPeak[1] = std::max(state.remoteOutputPeak[1], outputPeakR);
   InputAudioBus::instance().notePeak(inputPeak);
}

/** Mono (mid) output samples rendered elsewhere, for the scope and spectrum. */
EMSCRIPTEN_KEEPALIVE void bespoke_push_analysis_samples(const float* samples, int count)
{
   if (samples && count > 0)
      AudioAnalysis::pushSamples(samples, count);
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_offline_format(int bitsPerSample)
{
   if (bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32)
      gOfflineBits = bitsPerSample;
}

EMSCRIPTEN_KEEPALIVE int bespoke_render_offline(double seconds, int sampleRate)
{
   gOfflineWav.clear();
   auto& state = runtimeState();
   if (!state.canvas)
      return 0;
   AudioGraphSnapshot graph = state.canvas->createAudioGraphSnapshot();
   graph.transportPlaying = true;
   std::string error;
   if (!renderGraphOffline(graph, seconds, sampleRate > 0 ? sampleRate : 44100, gOfflineBits, gOfflineWav, error))
   {
      printf("BespokeSynth WASM: offline render failed: %s\n", error.c_str());
      return 0;
   }
   return 1;
}

EMSCRIPTEN_KEEPALIVE const unsigned char* bespoke_get_offline_wav(int* outByteLength)
{
   if (outByteLength)
      *outByteLength = static_cast<int>(gOfflineWav.size());
   return gOfflineWav.empty() ? nullptr : gOfflineWav.data();
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_offline_wav_size(void)
{
   return static_cast<int>(gOfflineWav.size());
}

EMSCRIPTEN_KEEPALIVE void bespoke_free_offline_render(void)
{
   gOfflineWav.clear();
   gOfflineWav.shrink_to_fit();
}

} // extern "C"
