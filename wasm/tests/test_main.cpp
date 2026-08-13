/**
 * BespokeSynth WASM - Test Suite
 * Basic tests for WASM build verification
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include <emscripten.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include "BespokeWasm/PixelFont.h"
#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/WasmPatchState.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include "BespokeWasm/adapters/FilterModuleAdapter.h"
#include "BespokeWasm/adapters/AdsrModuleAdapter.h"
#include "BespokeWasm/adapters/DelayModuleAdapter.h"
#include "BespokeWasm/adapters/NoiseModuleAdapter.h"
#include "BespokeWasm/adapters/StepSequencerModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/AudioHealth.h"

using namespace bespoke::wasm;

// Test results
static int gTestsPassed = 0;
static int gTestsFailed = 0;

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

// Basic math tests
void test_math()
{
   printf("\n=== Math Tests ===\n");

   TEST("Addition", 2 + 2 == 4);
   TEST("Multiplication", 3 * 4 == 12);
   TEST("Division", 10 / 2 == 5);

   float pi = 3.14159f;
   TEST("Float constant", pi > 3.14f && pi < 3.15f);
}

// Memory tests
void test_memory()
{
   printf("\n=== Memory Tests ===\n");

   int* arr = new int[100];
   TEST("Heap allocation", arr != nullptr);

   for (int i = 0; i < 100; i++)
   {
      arr[i] = i * 2;
   }

   TEST("Array write/read", arr[50] == 100);

   delete[] arr;
   TEST("Heap deallocation", true); // If we got here, it didn't crash
}

// String tests
void test_strings()
{
   printf("\n=== String Tests ===\n");

   std::string str = "Hello, WebAssembly!";
   TEST("String creation", !str.empty());
   TEST("String length", str.length() == 19);

   std::string concat = str + " Testing.";
   TEST("String concatenation", concat.length() > str.length());
}

// Vector tests
#include <vector>

void test_vectors()
{
   printf("\n=== Vector Tests ===\n");

   std::vector<float> vec;
   vec.push_back(1.0f);
   vec.push_back(2.0f);
   vec.push_back(3.0f);

   TEST("Vector push_back", vec.size() == 3);
   TEST("Vector access", vec[1] == 2.0f);

   vec.clear();
   TEST("Vector clear", vec.empty());
}

void test_pixel_font()
{
   printf("\n=== Pixel Font Tests ===\n");

   TEST("Glyph count", kPixelFontGlyphCount == 101);
   TEST("ASCII index for 'A'", pixelFontCharIndex('A') == 33);
   TEST("ASCII index for 'a'", pixelFontCharIndex('a') == 65);
   TEST("Lowercase index below clamp max", pixelFontCharIndex('z') == 90);
   TEST("Space index", pixelFontCharIndex(' ') == 0);

   const char* sharpUtf8 = "\xe2\x99\xaf";
   PixelFontGlyph sharpGlyph = pixelFontDecodeGlyph(sharpUtf8, 0, 3);
   TEST("UTF-8 sharp glyph index", sharpGlyph.index == kPixelFontGlyphSharp);
   TEST("UTF-8 sharp consumes 3 bytes", sharpGlyph.byteLength == 3);

   const float fontSize = 10.0f;
   const float spaceAdvance = pixelFontGlyphAdvance(0, fontSize);
   const float letterAdvance = pixelFontGlyphAdvance(pixelFontCharIndex('m'), fontSize);
   TEST("Space advance narrower than letters", spaceAdvance < letterAdvance);

   const float helloWidth = pixelFontTextWidth("hello", fontSize);
   const float approxFiveLetters = 5.0f * letterAdvance + 4.0f * fontSize * kPixelFontCharSpacingRatio;
   TEST("textWidth matches per-glyph advance", std::fabs(helloWidth - approxFiveLetters) < 0.01f);

   const float spacedWidth = pixelFontTextWidth("a b", fontSize);
   TEST("textWidth includes narrower space", spacedWidth > helloWidth && spacedWidth < pixelFontTextWidth("abc", fontSize));

   const uint32_t* cols = getPixelFontColumns();
   TEST("Font column table populated", cols != nullptr && cols[kPixelFontGlyphCount * 5 - 1] >= 0);
   TEST("Lowercase 'b' differs from uppercase 'B'",
        cols[pixelFontCharIndex('b') * 5] != cols[pixelFontCharIndex('B') * 5]);
}

// Audio buffer simulation
void test_audio_buffer()
{
   printf("\n=== Audio Buffer Tests ===\n");

   const int bufferSize = 512;
   const int numChannels = 2;

   float** buffer = new float*[numChannels];
   for (int ch = 0; ch < numChannels; ch++)
   {
      buffer[ch] = new float[bufferSize];
      for (int i = 0; i < bufferSize; i++)
      {
         buffer[ch][i] = 0.0f;
      }
   }

   TEST("Buffer allocation", buffer != nullptr);
   TEST("Buffer initialization", buffer[0][0] == 0.0f);

   // Generate sine wave
   float phase = 0.0f;
   float phaseInc = 2.0f * 3.14159f * 440.0f / 44100.0f;

   for (int i = 0; i < bufferSize; i++)
   {
      float sample = sinf(phase);
      buffer[0][i] = sample;
      buffer[1][i] = sample;
      phase += phaseInc;
   }

   TEST("Sine generation", buffer[0][0] != 0.0f || buffer[0][100] != 0.0f);

   for (int ch = 0; ch < numChannels; ch++)
   {
      delete[] buffer[ch];
   }
   delete[] buffer;

   TEST("Buffer cleanup", true);
}

static float buffer_rms(const std::vector<float>& buffer)
{
   double sum = 0.0;
   for (float sample : buffer)
      sum += sample * sample;
   return std::sqrt(sum / static_cast<double>(buffer.size()));
}

static AudioGraphSnapshot make_test_graph(float gain, float cutoff, float lfoDepth)
{
   AudioGraphSnapshot graph;
   graph.transportPlaying = true;

   AudioGraphNode osc;
   osc.id = 1;
   osc.type = "oscillator";
   osc.frequency = 440.0f;
   osc.volume = 0.8f;
   osc.waveform = 1;
   graph.nodes.push_back(osc);

   AudioGraphNode filter;
   filter.id = 2;
   filter.type = "filter";
   filter.cutoff = cutoff;
   filter.resonance = 0.2f;
   filter.filterType = 0;
   graph.nodes.push_back(filter);

   AudioGraphNode gainNode;
   gainNode.id = 3;
   gainNode.type = "gain";
   gainNode.gain = gain;
   graph.nodes.push_back(gainNode);

   AudioGraphNode lfo;
   lfo.id = 4;
   lfo.type = "lfo";
   lfo.lfoRate = 6.0f;
   lfo.lfoDepth = lfoDepth;
   lfo.lfoShape = 0;
   graph.nodes.push_back(lfo);

   AudioGraphNode out;
   out.id = 5;
   out.type = "output";
   graph.nodes.push_back(out);

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

static std::vector<float> render_graph(AudioGraphSnapshot graph)
{
   constexpr int bufferSize = 1024;
   AudioGraphEngine engine;
   std::vector<float> left(bufferSize, 0.0f);
   std::vector<float> right(bufferSize, 0.0f);
   float* outputs[2] = { left.data(), right.data() };
   engine.processBlock(graph, outputs, 2, bufferSize, 44100.0f);
   return left;
}

void test_audio_graph_engine()
{
   printf("\n=== Audio Graph Engine Tests ===\n");

   auto nominal = render_graph(make_test_graph(1.0f, 1800.0f, 0.0f));
   TEST("Graph renders non-silent output", buffer_rms(nominal) > 0.01f);

   auto quiet = render_graph(make_test_graph(0.25f, 1800.0f, 0.0f));
   TEST("Gain node scales graph output", buffer_rms(quiet) < buffer_rms(nominal) * 0.35f);

   auto filtered = render_graph(make_test_graph(1.0f, 120.0f, 0.0f));
   TEST("Filter node affects timbre/level", buffer_rms(filtered) < buffer_rms(nominal) * 0.75f);

   auto modulated = render_graph(make_test_graph(1.0f, 400.0f, 1.0f));
   double diff = 0.0;
   for (size_t i = 0; i < filtered.size(); ++i)
      diff += std::fabs(filtered[i] - modulated[i]);
   TEST("LFO CV changes filter output", diff > 1.0);

   auto stoppedGraph = make_test_graph(1.0f, 1800.0f, 0.0f);
   stoppedGraph.transportPlaying = false;
   auto stopped = render_graph(stoppedGraph);
   TEST("Stopped transport silences graph", buffer_rms(stopped) == 0.0f);
}

void test_patch_state_json()
{
   printf("\n=== Patch State JSON Tests ===\n");

   ModuleCanvas::StateSnapshot snapshot;
   snapshot.transportBPM = 128.0f;
   snapshot.transportPlaying = true;
   snapshot.offsetX = 12.0f;
   snapshot.offsetY = 34.0f;
   snapshot.scale = 1.25f;

   ModuleCanvas::StateModule osc;
   osc.id = 10;
   osc.type = "oscillator";
   osc.x = 100.0f;
   osc.y = 200.0f;
   osc.controls["frequency"] = 330.0f;
   osc.controls["volume"] = 0.5f;
   snapshot.modules.push_back(osc);

   ModuleCanvas::StateModule gain;
   gain.id = 20;
   gain.type = "gain";
   gain.x = 300.0f;
   gain.y = 210.0f;
   gain.controls["gain"] = 0.25f;
   snapshot.modules.push_back(gain);

   ModuleCanvas::StateModule seq;
   seq.id = 30;
   seq.type = "stepsequencer";
   seq.x = 80.0f;
   seq.y = 300.0f;
   seq.controls["pattern"] = static_cast<float>(0xA5A5);
   seq.controls["pitch"] = 67.0f;
   seq.controls["gate"] = 0.6f;
   seq.controls["steps"] = 16.0f;
   snapshot.modules.push_back(seq);

   ModuleCanvas::StateConnection conn;
   conn.sourceModuleId = 10;
   conn.destModuleId = 20;
   conn.sourcePortIndex = 0;
   conn.destPortIndex = 0;
   snapshot.connections.push_back(conn);

   ModuleCanvas::StateConnection noteConn;
   noteConn.sourceModuleId = 30;
   noteConn.destModuleId = 10;
   noteConn.sourcePortIndex = 0;
   noteConn.destPortIndex = 0;
   snapshot.connections.push_back(noteConn);

   const std::string json = serializePatchState(snapshot, 0);
   TEST("State JSON contains modules", json.find("\"modules\"") != std::string::npos);
   TEST("State JSON contains connections", json.find("\"connections\"") != std::string::npos);
   TEST("State JSON uses .bsk-compatible filetype", json.find("\"filetype\": \"bespokesynth\"") != std::string::npos);
   TEST("State JSON contains stepsequencer", json.find("\"stepsequencer\"") != std::string::npos);

   ModuleCanvas::StateSnapshot loaded;
   int viewMode = 1;
   std::string error;
   TEST("State JSON deserializes", deserializePatchState(json, loaded, viewMode, error));
   TEST("State JSON preserves view mode", viewMode == 0);
   TEST("State JSON preserves module count", loaded.modules.size() == 3);
   TEST("State JSON preserves connection count", loaded.connections.size() == 2);
   TEST("State JSON preserves transport BPM", std::fabs(loaded.transportBPM - 128.0f) < 0.01f);
   TEST("State JSON preserves control value", std::fabs(loaded.modules[0].controls["frequency"] - 330.0f) < 0.01f);

   const ModuleCanvas::StateModule* loadedSeq = nullptr;
   for (const auto& module : loaded.modules)
   {
      if (module.type == "stepsequencer")
         loadedSeq = &module;
   }
   TEST("State JSON restores sequencer module", loadedSeq != nullptr);
   if (loadedSeq)
   {
      TEST("State JSON restores sequencer pattern",
           std::fabs(loadedSeq->controls.at("pattern") - static_cast<float>(0xA5A5)) < 0.01f);
      TEST("State JSON restores sequencer pitch",
           std::fabs(loadedSeq->controls.at("pitch") - 67.0f) < 0.01f);
   }
}

void test_filter_module_adapter()
{
   printf("\n=== Filter Module Adapter Tests ===\n");

   const FilterModuleAdapter adapter;
   TEST("Filter adapter type id", std::string(adapter.typeId()) == "filter");
   TEST("Filter adapter audio role", adapter.audioRole() == WasmAudioRole::AudioProcessor);

   std::map<std::string, float> controls = { { "cutoff", 800.0f }, { "resonance", 0.4f }, { "type", 1.0f } };
   AudioGraphNode node;
   node.id = 42;
   node.type = "filter";
   adapter.fillAudioGraphNode(controls, node);
   TEST("Filter adapter fills cutoff", std::fabs(node.cutoff - 800.0f) < 0.01f);
   TEST("Filter adapter fills resonance", std::fabs(node.resonance - 0.4f) < 0.01f);
   TEST("Filter adapter fills filter type", node.filterType == 1);

   FilterAdapterRuntimeState runtime{};
   adapter.initRuntimeState(&runtime);

   float buffer[64] = {};
   for (int i = 0; i < 64; ++i)
      buffer[i] = std::sinf(static_cast<float>(i) * 0.2f);

   WasmAudioProcessContext context;
   context.sampleRate = 44100.0f;
   context.numSamples = 64;
   adapter.processAudio(&runtime, node, buffer, context);
   double energy = 0.0;
   for (float sample : buffer)
      energy += sample * sample;
   TEST("Filter adapter processes audio block", energy > 0.0);

   auto ui = adapter.createUiModule(7);
   TEST("Filter adapter creates UI module", ui != nullptr);
   if (ui)
   {
      ui->setControlValue("cutoff", 500.0f);
      TEST("Filter UI module stores cutoff", std::fabs(ui->getControlValue("cutoff") - 500.0f) < 0.01f);
   }
}

void test_first_wave_module_adapters()
{
   printf("\n=== First-Wave Module Adapter Tests ===\n");

   {
      NoiseModuleAdapter adapter;
      AudioGraphNode node;
      node.type = "noise";
      adapter.fillAudioGraphNode({ { "volume", 0.5f }, { "color", 0.0f } }, node);
      NoiseAdapterRuntimeState runtime{};
      adapter.initRuntimeState(&runtime);
      float buffer[256] = {};
      WasmAudioProcessContext context;
      context.sampleRate = 44100.0f;
      context.numSamples = 256;
      adapter.processAudio(&runtime, node, buffer, context);
      TEST("Noise adapter produces non-silent output", buffer_rms(std::vector<float>(buffer, buffer + 256)) > 0.01f);
      TEST("Noise adapter registered", WasmModuleAdapterRegistry::instance().find("noise") != nullptr);
   }

   {
      DelayModuleAdapter adapter;
      AudioGraphNode node;
      node.type = "delay";
      adapter.fillAudioGraphNode({ { "time", 0.01f }, { "feedback", 0.5f }, { "mix", 0.5f } }, node);
      DelayAdapterRuntimeState runtime{};
      adapter.initRuntimeState(&runtime);

      float buffer[512] = {};
      buffer[0] = 1.0f;
      WasmAudioProcessContext context;
      context.sampleRate = 44100.0f;
      context.numSamples = 512;
      adapter.processAudio(&runtime, node, buffer, context);
      double tail = 0.0;
      for (int i = 100; i < 512; ++i)
         tail += std::fabs(buffer[i]);
      TEST("Delay adapter leaves echo energy", tail > 0.01);
      TEST("Delay adapter registered", WasmModuleAdapterRegistry::instance().find("delay") != nullptr);
   }

   {
      AdsrModuleAdapter adapter;
      AudioGraphNode node;
      node.type = "adsr";
      adapter.fillAudioGraphNode(
         { { "attack", 5.0f }, { "decay", 40.0f }, { "sustain", 0.4f }, { "release", 80.0f } }, node);
      AdsrAdapterRuntimeState runtime{};
      adapter.initRuntimeState(&runtime);

      float buffer[256];
      for (int i = 0; i < 256; ++i)
         buffer[i] = 1.0f;

      WasmNoteEvent noteOn{ 60, 1.0f, true };
      WasmAudioProcessContext context;
      context.sampleRate = 44100.0f;
      context.numSamples = 256;
      context.blockStartTimeSeconds = 1.0;
      context.notes = &noteOn;
      context.noteCount = 1;
      adapter.processAudio(&runtime, node, buffer, context);
      TEST("ADSR adapter applies envelope after note-on", buffer_rms(std::vector<float>(buffer, buffer + 256)) > 0.01f);
      TEST("ADSR adapter registered", WasmModuleAdapterRegistry::instance().find("adsr") != nullptr);

      auto ui = adapter.createUiModule(3);
      TEST("ADSR UI module created", ui != nullptr);
      if (ui)
      {
         ui->setControlValue("attack", 25.0f);
         TEST("ADSR UI stores attack", std::fabs(ui->getControlValue("attack") - 25.0f) < 0.01f);
      }
   }

   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;

      AudioGraphNode noise;
      noise.id = 1;
      noise.type = "noise";
      noise.noiseVolume = 0.4f;
      graph.nodes.push_back(noise);

      AudioGraphNode delay;
      delay.id = 2;
      delay.type = "delay";
      delay.delayTime = 0.05f;
      delay.delayFeedback = 0.2f;
      delay.delayMix = 0.4f;
      graph.nodes.push_back(delay);

      AudioGraphNode out;
      out.id = 3;
      out.type = "output";
      graph.nodes.push_back(out);

      AudioGraphConnection c1;
      c1.sourceModuleId = 1;
      c1.destModuleId = 2;
      c1.sourcePortType = PortType::Audio;
      c1.destPortType = PortType::Audio;
      graph.connections.push_back(c1);

      AudioGraphConnection c2;
      c2.sourceModuleId = 2;
      c2.destModuleId = 3;
      c2.sourcePortType = PortType::Audio;
      c2.destPortType = PortType::Audio;
      graph.connections.push_back(c2);

      auto rendered = render_graph(graph);
      TEST("Noise→delay→output graph is non-silent", buffer_rms(rendered) > 0.005f);
   }
}

void test_step_sequencer_note_graph()
{
   printf("\n=== Step Sequencer Note Graph Tests ===\n");

   TEST("Step sequencer registered",
        WasmModuleAdapterRegistry::instance().find("stepsequencer") != nullptr);
   TEST("Step sequencer is NoteSource",
        WasmModuleAdapterRegistry::instance().find("stepsequencer")->audioRole() ==
           WasmAudioRole::NoteSource);

   StepSequencerModuleAdapter adapter;
   AudioGraphNode seqNode;
   seqNode.id = 1;
   seqNode.type = "stepsequencer";
   adapter.fillAudioGraphNode(
      { { "pattern", 1.0f }, { "pitch", 72.0f }, { "gate", 0.5f }, { "steps", 16.0f } }, seqNode);
   TEST("Sequencer fills pattern mask", seqNode.patternMask == 1);
   TEST("Sequencer fills pitch", seqNode.seqPitch == 72);

   auto ui = adapter.createUiModule(9);
   TEST("Sequencer UI created", ui != nullptr);
   if (ui)
   {
      ui->setControlValue("pattern", static_cast<float>(0x00FF));
      TEST("Sequencer UI stores pattern", std::fabs(ui->getControlValue("pattern") - 255.0f) < 0.01f);
      auto* seqUi = dynamic_cast<StepSequencerModule*>(ui.get());
      TEST("Sequencer UI toggles steps", seqUi != nullptr);
      if (seqUi)
      {
         TEST("Step 0 initially on in 0xFF", seqUi->isStepActive(0));
         seqUi->setStepActive(0, false);
         TEST("Step 0 toggled off", !seqUi->isStepActive(0));
      }
   }

   AudioGraphSnapshot graph;
   graph.transportPlaying = true;
   graph.transportBPM = 120.0f;

   AudioGraphNode seq;
   seq.id = 1;
   seq.type = "stepsequencer";
   seq.patternMask = 0x1; // only step 0
   seq.seqPitch = 60;
   seq.gateLength = 0.5f;
   seq.steps = 16;
   graph.nodes.push_back(seq);

   AudioGraphNode osc;
   osc.id = 2;
   osc.type = "oscillator";
   osc.volume = 1.0f;
   osc.frequency = 440.0f;
   graph.nodes.push_back(osc);

   AudioGraphNode out;
   out.id = 3;
   out.type = "output";
   graph.nodes.push_back(out);

   AudioGraphConnection noteConn;
   noteConn.sourceModuleId = 1;
   noteConn.destModuleId = 2;
   noteConn.sourcePortType = PortType::Note;
   noteConn.destPortType = PortType::Note;
   graph.connections.push_back(noteConn);

   AudioGraphConnection audioConn;
   audioConn.sourceModuleId = 2;
   audioConn.destModuleId = 3;
   audioConn.sourcePortType = PortType::Audio;
   audioConn.destPortType = PortType::Audio;
   graph.connections.push_back(audioConn);

   // At 120 BPM, one 16th note = 0.125s = 5512.5 samples @ 44100.
   // Render ~1 beat (4 sixteenths) in 512-sample blocks and track energy.
   AudioGraphEngine engine;
   constexpr int bufferSize = 512;
   constexpr int blocks = 180; // ~2 seconds
   double energyOn = 0.0;
   double energyOff = 0.0;
   int onBlocks = 0;
   int offBlocks = 0;
   for (int b = 0; b < blocks; ++b)
   {
      std::vector<float> left(bufferSize, 0.0f);
      std::vector<float> right(bufferSize, 0.0f);
      float* outputs[2] = { left.data(), right.data() };
      engine.processBlock(graph, outputs, 2, bufferSize, 44100.0f);
      const float rms = buffer_rms(left);
      // Roughly first half of each quarter-note window is gated on for step 0 only pattern
      // across a bar: active near beat starts.
      if (rms > 0.01f)
      {
         energyOn += rms;
         ++onBlocks;
      }
      else
      {
         energyOff += 1.0;
         ++offBlocks;
      }
   }

   TEST("Sequencer→osc graph produces gated notes", onBlocks > 5);
   TEST("Sequencer→osc graph has silent gaps between steps", offBlocks > 5);
   TEST("Sequencer gated energy is positive", energyOn > 0.05);

   // Empty pattern with note cable should stay silent (no free-run).
   graph.nodes[0].patternMask = 0;
   auto silent = render_graph(graph);
   TEST("Empty pattern with note cable stays silent", buffer_rms(silent) == 0.0f);
}

void test_audio_health_metrics()
{
   printf("\n=== Audio Health Metrics Tests ===\n");

   audioHealthReset();
   audioHealthSetBackend(0);
   audioHealthOnCallback(0.001, 0.01, 0.011); // under budget
   audioHealthOnCallback(0.02, 0.01, 0.012);  // overrun → underrun
   audioHealthSetQueueStats(512, 4096, 3);

   const AudioHealthSnapshot snap = audioHealthSnapshot();
   TEST("Health callback count advances", snap.callbackCount == 2);
   TEST("Health records budget overrun as underrun", snap.underrunCount >= 1);
   TEST("Health stores queue depth", snap.queueDepthFrames == 512);
   TEST("Health stores capacity", snap.capacityFrames == 4096);
   TEST("Health stores external underruns", snap.externalUnderrunCount == 3);
   TEST("Health max process time positive", snap.maxProcessTimeMs > 0.0);

   const char* json = audioHealthJson();
   TEST("Health JSON non-empty", json && std::strlen(json) > 10);
   TEST("Health JSON mentions cpuLoad", std::strstr(json, "cpuLoad") != nullptr);
}

int main()
{
   printf("BespokeSynth WASM Test Suite\n");
   printf("============================\n");

   test_math();
   test_memory();
   test_strings();
   test_vectors();
   test_pixel_font();
   test_audio_buffer();
   test_audio_graph_engine();
   test_filter_module_adapter();
   test_first_wave_module_adapters();
   test_step_sequencer_note_graph();
   test_patch_state_json();
   test_audio_health_metrics();

   printf("\n============================\n");
   printf("Results: %d passed, %d failed\n", gTestsPassed, gTestsFailed);
   printf("============================\n");

   return gTestsFailed > 0 ? 1 : 0;
}
