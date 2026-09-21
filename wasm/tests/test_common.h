// Shared includes, TEST macro, and helpers for the WASM unit test suite.
#pragma once

#include <emscripten.h>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <cstring>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include "BespokeWasm/PixelFont.h"
#include "BespokeWasm/SdfFont.h"
#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/AudioProcessPlan.h"
#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/WasmPatchState.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include "BespokeWasm/adapters/FilterModuleAdapter.h"
#include "BespokeWasm/adapters/AdsrModuleAdapter.h"
#include "BespokeWasm/adapters/DelayModuleAdapter.h"
#include "BespokeWasm/adapters/NoiseModuleAdapter.h"
#include "BespokeWasm/adapters/StepSequencerModuleAdapter.h"
#include "BespokeWasm/adapters/OscillatorModuleAdapter.h"
#include "BespokeWasm/adapters/GainModuleAdapter.h"
#include "BespokeWasm/adapters/ReverbModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/AudioHealth.h"
#include "BespokeWasm/AudioRtGuard.h"
#include "BespokeWasm/SampleBuffer.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/OfflineRender.h"
#include "BespokeWasm/adapters/SamplerModuleAdapter.h"
#include "BespokeWasm/adapters/LooperModuleAdapter.h"
#include "BespokeWasm/adapters/PulseToNoteModuleAdapter.h"
#include "BespokeWasm/adapters/TransportModuleAdapter.h"
#include "BespokeWasm/adapters/MixerModuleAdapter.h"
#include "BespokeWasm/adapters/CompressorModuleAdapter.h"
#include "BespokeWasm/adapters/ChorusModuleAdapter.h"
#include "BespokeWasm/adapters/WaveshaperModuleAdapter.h"
#include "BespokeWasm/adapters/ClockDividerModuleAdapter.h"
#include "BespokeWasm/adapters/DrumSeqModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include "BespokeWasm/ScaleQuantize.h"
#include "BespokeWasm/MidiMap.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/AudioAnalysis.h"
#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/ScriptNoteQueue.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/TuningTable.h"
#include "BespokeWasm/UndoStack.h"
#include "BespokeWasm/adapters/ScriptModuleAdapter.h"
#include <algorithm>
#include <tuple>

using namespace bespoke::wasm;

extern int gTestsPassed;
extern int gTestsFailed;

#define TEST(name, condition)         \
   do                                 \
   {                                  \
      if (condition)                  \
      {                               \
         printf("[PASS] %s\n", name); \
         gTestsPassed++;              \
      }                               \
      else                            \
      {                               \
         printf("[FAIL] %s\n", name); \
         gTestsFailed++;              \
      }                               \
   } while (0)

// Test groups (one per translation unit; run from main in test_main.cpp).
void test_math();
void test_memory();
void test_strings();
void test_vectors();
void test_pixel_font();
void test_sdf_font();
void test_audio_buffer();
void test_audio_graph_engine();
void test_audio_process_plan();
void test_audio_graph_rt_allocations();
void test_audio_graph_benchmark();
void test_audio_health_metrics();
void test_filter_module_adapter();
void test_first_wave_module_adapters();
void test_step_sequencer_note_graph();
void test_typed_adapter_round_trip();
void test_reverb_adapter();
void test_patch_state_json();
void test_patch_schema_v1_migration();
void test_sample_io_and_offline();
void test_graph_contract_pulse_voices_midi_scale();
void test_catalog_wave2_registration();
void test_catalog_wave2_mixer_ports();
void test_catalog_wave2_dynamics_and_colour();
void test_catalog_wave2_pulse_and_drums();
void test_catalog_wave2_freeverb();
void test_catalog_wave2_saved_state_fixture();
void test_dsp_simd_kernels();
void test_scala_tuning();
void test_undo_redo();
void test_midi_learn_and_channel();
void test_script_note_queue();

// --- Shared helpers ---

inline float buffer_rms(const std::vector<float>& buffer)
{
   double sum = 0.0;
   for (float sample : buffer)
      sum += sample * sample;
   return std::sqrt(sum / static_cast<double>(buffer.size()));
}

inline AudioGraphSnapshot make_test_graph(float gain, float cutoff, float lfoDepth)
{
   AudioGraphSnapshot graph;
   graph.transportPlaying = true;

   appendAudioGraphNode(graph, 1, "oscillator", true,
                        { { "frequency", 440.0f }, { "volume", 0.8f }, { "waveform", 1.0f } });
   appendAudioGraphNode(graph, 2, "filter", true,
                        { { "cutoff", cutoff }, { "resonance", 0.2f }, { "type", 0.0f } });
   appendAudioGraphNode(graph, 3, "gain", true, { { "gain", gain } });
   appendAudioGraphNode(graph, 4, "lfo", true,
                        { { "rate", 6.0f }, { "depth", lfoDepth }, { "shape", 0.0f } });
   appendAudioGraphNode(graph, 5, "output", true, {});

   auto audioConnection = [](int src, int dst)
   {
      AudioGraphConnection conn;
      conn.sourceModuleId = src;
      conn.destModuleId = dst;
      conn.sourcePortType = PortType::Audio;
      conn.destPortType = PortType::Audio;
      return conn;
   };
   graph.connections.push_back(audioConnection(1, 2));
   graph.connections.push_back(audioConnection(2, 3));
   graph.connections.push_back(audioConnection(3, 5));

   AudioGraphConnection cv;
   cv.sourceModuleId = 4;
   cv.destModuleId = 2;
   cv.sourcePortType = PortType::Modulation;
   cv.destPortType = PortType::Modulation;
   cv.destPortIndex = 1;
   graph.connections.push_back(cv);

   return graph;
}

inline std::vector<float> render_graph(AudioGraphSnapshot graph, AudioGraphEngine* engine = nullptr)
{
   constexpr int bufferSize = 1024;
   compileAudioProcessPlan(graph, graph.processPlan);
   AudioGraphEngine localEngine;
   AudioGraphEngine& activeEngine = engine ? *engine : localEngine;
   activeEngine.prepareForBlock(bufferSize, 128, 32);
   std::vector<float> left(bufferSize, 0.0f);
   std::vector<float> right(bufferSize, 0.0f);
   float* outputs[2] = { left.data(), right.data() };
   activeEngine.processBlock(graph, outputs, 2, bufferSize, 44100.0f);
   return left;
}

inline AudioGraphConnection typed_connection(int src, int srcPort, int dst, int dstPort)
{
   AudioGraphConnection conn;
   conn.sourceModuleId = src;
   conn.sourcePortIndex = srcPort;
   conn.destModuleId = dst;
   conn.destPortIndex = dstPort;
   return conn;
}

// Resolve port types from the registered adapters, like ModuleCanvas::buildAudioGraphSnapshotLocked.
inline void resolve_port_types(AudioGraphSnapshot& graph, const std::map<int, std::string>& typeById)
{
   auto& registry = WasmModuleAdapterRegistry::instance();
   for (auto& conn : graph.connections)
   {
      const WasmModuleAdapter* src = registry.find(typeById.at(conn.sourceModuleId));
      const WasmModuleAdapter* dst = registry.find(typeById.at(conn.destModuleId));
      conn.sourcePortType = src->outputPorts()[static_cast<size_t>(conn.sourcePortIndex)].type;
      conn.destPortType = dst->inputPorts()[static_cast<size_t>(conn.destPortIndex)].type;
   }
}

inline std::vector<float> render_blocks(AudioGraphSnapshot& graph, int blocks, int blockSize = 512)
{
   compileAudioProcessPlan(graph, graph.processPlan);
   AudioGraphEngine engine;
   engine.prepareForBlock(blockSize, 128, 32);
   std::vector<float> all;
   all.reserve(static_cast<size_t>(blocks * blockSize));
   std::vector<float> left(static_cast<size_t>(blockSize));
   std::vector<float> right(static_cast<size_t>(blockSize));
   for (int b = 0; b < blocks; ++b)
   {
      float* outputs[2] = { left.data(), right.data() };
      engine.processBlock(graph, outputs, 2, blockSize, 44100.0f);
      all.insert(all.end(), left.begin(), left.end());
   }
   return all;
}

// Silent→sounding transitions at block granularity (gated oscillator, no envelope).
inline int count_onsets(const std::vector<float>& audio, int blockSize = 512)
{
   int onsets = 0;
   bool sounding = false;
   for (size_t start = 0; start + static_cast<size_t>(blockSize) <= audio.size(); start += static_cast<size_t>(blockSize))
   {
      float peak = 0.0f;
      for (int i = 0; i < blockSize; ++i)
         peak = std::max(peak, std::fabs(audio[start + static_cast<size_t>(i)]));
      const bool now = peak > 0.01f;
      if (now && !sounding)
         ++onsets;
      sounding = now;
   }
   return onsets;
}

inline bool all_finite(const std::vector<float>& audio)
{
   for (float v : audio)
   {
      if (!std::isfinite(v))
         return false;
   }
   return true;
}
