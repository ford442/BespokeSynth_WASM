#include "test_common.h"

void test_graph_contract_pulse_voices_midi_scale()
{
   printf("\n=== Graph Contract (Pulse / Voices / MIDI / Scale) ===\n");
   fflush(stdout);

   const WasmModuleAdapter* pulseToNote = WasmModuleAdapterRegistry::instance().find("pulsetonote");
   const WasmModuleAdapter* transport = WasmModuleAdapterRegistry::instance().find("transport");
   TEST("PulseToNote registered", pulseToNote != nullptr);
   TEST("Transport is PulseSource", transport != nullptr && transport->audioRole() == WasmAudioRole::PulseSource);

   AudioGraphSnapshot pulseGraph;
   pulseGraph.transportPlaying = true;
   pulseGraph.transportBPM = 120.0f;
   appendAudioGraphNode(pulseGraph, 1, "transport", true, {});
   appendAudioGraphNode(pulseGraph, 2, "pulsetonote", true, { { "pitch", 60.0f }, { "gate", 0.75f } });
   appendAudioGraphNode(pulseGraph, 3, "oscillator", true, { { "volume", 1.0f }, { "frequency", 440.0f } });
   appendAudioGraphNode(pulseGraph, 4, "output", true, {});
   pulseGraph.connections.push_back({ 1, 0, 2, 0, PortType::Pulse, PortType::Pulse });
   pulseGraph.connections.push_back({ 2, 0, 3, 0, PortType::Note, PortType::Note });
   pulseGraph.connections.push_back({ 3, 0, 4, 0, PortType::Audio, PortType::Audio });
   compileAudioProcessPlan(pulseGraph, pulseGraph.processPlan);
   TEST("Pulse plan is valid", pulseGraph.processPlan.valid);
   TEST("Pulse plan has pulse source", !pulseGraph.processPlan.pulseSourceNodeIndices.empty());
   bool oscHasNoteCable = false;
   for (const auto& step : pulseGraph.processPlan.steps)
   {
      if (step.moduleId == 3)
         oscHasNoteCable = step.hasNoteCable;
   }
   TEST("Oscillator step has note cable from PulseToNote", oscHasNoteCable);

   AudioGraphEngine pulseEngine;
   pulseEngine.prepareForBlock(512, 128, 32);
   double pulseEnergy = 0.0;
   for (int block = 0; block < 80; ++block)
   {
      std::vector<float> left(512, 0.0f);
      std::vector<float> right(512, 0.0f);
      float* outputs[2] = { left.data(), right.data() };
      pulseEngine.processBlock(pulseGraph, outputs, 2, 512, 44100.0f);
      pulseEnergy += buffer_rms(left);
   }
   TEST("Transport Pulse→PulseToNote→Osc is audible", pulseEnergy > 0.01);

   AudioGraphSnapshot silentPulse = pulseGraph;
   silentPulse.connections.erase(silentPulse.connections.begin());
   compileAudioProcessPlan(silentPulse, silentPulse.processPlan);
   AudioGraphEngine silentEngine;
   silentEngine.prepareForBlock(512, 128, 32);
   double silentEnergy = 0.0;
   for (int block = 0; block < 40; ++block)
   {
      std::vector<float> left(512, 0.0f);
      std::vector<float> right(512, 0.0f);
      float* outputs[2] = { left.data(), right.data() };
      silentEngine.processBlock(silentPulse, outputs, 2, 512, 44100.0f);
      silentEnergy += buffer_rms(left);
   }
   TEST("Unconnected PulseToNote stays silent", silentEnergy < 0.0001);

   OscillatorModuleAdapter oscAdapter;
   std::vector<uint8_t> oscRuntime(oscAdapter.runtimeStateSize());
   oscAdapter.initRuntimeState(oscRuntime.data());
   OscillatorParams oscParams{};
   oscParams.volume = 1.0f;
   WasmNoteEvent chord[2] = { { 60, 1.0f, true }, { 72, 1.0f, true } };
   WasmAudioProcessContext oscContext;
   oscContext.sampleRate = 44100.0f;
   oscContext.numSamples = 64;
   oscContext.notes = chord;
   oscContext.noteCount = 2;
   float oscBuffer[64] = {};
   oscAdapter.processMono(oscRuntime.data(), &oscParams, oscBuffer, oscContext);
   auto* oscState = reinterpret_cast<OscillatorAdapterRuntimeState*>(oscRuntime.data());
   int gated = 0;
   for (int i = 0; i < kOscillatorVoiceCount; ++i)
   {
      if (oscState->voices[i].gated)
         ++gated;
   }
   TEST("Two note-ons allocate two oscillator voices", gated == 2);

   WasmNoteEvent off60{ 60, 0.0f, false };
   oscContext.notes = &off60;
   oscContext.noteCount = 1;
   oscAdapter.processMono(oscRuntime.data(), &oscParams, oscBuffer, oscContext);
   gated = 0;
   int remainingPitch = -1;
   for (int i = 0; i < kOscillatorVoiceCount; ++i)
   {
      if (oscState->voices[i].gated)
      {
         ++gated;
         remainingPitch = oscState->voices[i].pitch;
      }
   }
   TEST("Matching note-off releases only one voice", gated == 1 && remainingPitch == 72);
   oscAdapter.destroyRuntimeState(oscRuntime.data());

   AudioGraphSnapshot midiGraph;
   midiGraph.transportPlaying = true;
   appendAudioGraphNode(midiGraph, 1, "oscillator", true, { { "volume", 1.0f }, { "frequency", 440.0f } });
   appendAudioGraphNode(midiGraph, 2, "output", true, {});
   midiGraph.connections.push_back({ 1, 0, 2, 0, PortType::Audio, PortType::Audio });
   AudioGraphEngine midiEngine;
   midiEngine.queueNote({ 60, 1.0f, true, 0.0, 0 });
   midiEngine.queueNote({ 64, 1.0f, true, 0.0, 0 });
   auto midiOut = render_graph(midiGraph, &midiEngine);
   TEST("Overlapping MIDI notes on uncabled oscillator produce audio", buffer_rms(midiOut) > 0.01f);

   TEST("Chromatic quantization is identity",
        quantizeMidiPitch(60, { 0, kScaleModeChromatic }) == 60);
   TEST("C major leaves C", quantizeMidiPitch(60, { 0, kScaleModeMajor }) == 60);
   TEST("D major snaps C toward scale", quantizeMidiPitch(60, { 2, kScaleModeMajor }) != 60);

   AudioGraphSnapshot seqC{};
   seqC.transportPlaying = true;
   seqC.transportBPM = 120.0f;
   seqC.musicScale = { 0, kScaleModeChromatic };
   appendAudioGraphNode(seqC, 1, "stepsequencer", true,
                        { { "pattern", 1.0f }, { "pitch", 61.0f }, { "gate", 0.9f }, { "steps", 16.0f } });
   appendAudioGraphNode(seqC, 2, "oscillator", true, { { "volume", 1.0f }, { "frequency", 440.0f } });
   appendAudioGraphNode(seqC, 3, "output", true, {});
   seqC.connections.push_back({ 1, 0, 2, 0, PortType::Note, PortType::Note });
   seqC.connections.push_back({ 2, 0, 3, 0, PortType::Audio, PortType::Audio });
   AudioGraphSnapshot seqMajor = seqC;
   seqMajor.musicScale = { 0, kScaleModeMajor };
   AudioGraphEngine seqEngineA;
   AudioGraphEngine seqEngineB;
   seqEngineA.prepareForBlock(512, 128, 32);
   seqEngineB.prepareForBlock(512, 128, 32);
   compileAudioProcessPlan(seqC, seqC.processPlan);
   compileAudioProcessPlan(seqMajor, seqMajor.processPlan);
   double diff = 0.0;
   for (int block = 0; block < 40; ++block)
   {
      std::vector<float> a(512, 0.0f);
      std::vector<float> b(512, 0.0f);
      std::vector<float> ar(512, 0.0f);
      std::vector<float> br(512, 0.0f);
      float* ao[2] = { a.data(), ar.data() };
      float* bo[2] = { b.data(), br.data() };
      seqEngineA.processBlock(seqC, ao, 2, 512, 44100.0f);
      seqEngineB.processBlock(seqMajor, bo, 2, 512, 44100.0f);
      for (size_t i = 0; i < a.size(); ++i)
         diff += std::fabs(a[i] - b[i]);
   }
   TEST("Scale snapshot changes sequencer/oscillator pitch", diff > 1.0);

   std::vector<MidiCcBinding> emptyMap;
   int hits = 0;
   forEachMatchingMidiCc(emptyMap, 0, 7, 1.0f, [&](int, const std::string&, float)
                         { ++hits; });
   TEST("Empty CC map is a no-op", hits == 0);

   ModuleCanvas::StateSnapshot midiSnap;
   midiSnap.schemaVersion = kPatchSchemaVersion;
   ModuleCanvas::StateModule gainMod;
   gainMod.id = 20;
   gainMod.type = "gain";
   gainMod.controls["gain"] = 0.25f;
   midiSnap.modules.push_back(gainMod);
   midiSnap.midiCcMap.push_back({ -1, 7, 20, "gain", 0.0f, 1.0f });
   const std::string midiJson = serializePatchState(midiSnap, 0);
   TEST("Patch JSON stores midi map", midiJson.find("\"midi\"") != std::string::npos);

   ModuleCanvas::StateSnapshot midiLoaded;
   int viewMode = 0;
   std::string error;
   TEST("MIDI map deserializes", deserializePatchState(midiJson, midiLoaded, viewMode, error));
   TEST("MIDI map round-trips module id", midiLoaded.midiCcMap.size() == 1 &&
                                            midiLoaded.midiCcMap[0].moduleId == 20 &&
                                            midiLoaded.midiCcMap[0].cc == 7);

   const char* v3Json =
   "{\n"
   "  \"filetype\": \"bespokesynth\",\n"
   "  \"format\": \"bespokesynth-wasm-state\",\n"
   "  \"version\": 3,\n"
   "  \"modules\": [\n"
   "    { \"id\": 20, \"type\": \"gain\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"gain\": 0.5 } }\n"
   "  ],\n"
   "  \"connections\": []\n"
   "}\n";
   ModuleCanvas::StateSnapshot v3Loaded;
   TEST("V3 patch deserializes", deserializePatchState(v3Json, v3Loaded, viewMode, error));
   TEST("V3 patch seeds default CC7→gain", !v3Loaded.midiCcMap.empty() && v3Loaded.midiCcMap[0].cc == 7 &&
                                              v3Loaded.midiCcMap[0].moduleId == 20);

   const char* emptyMapJson =
   "{\n"
   "  \"filetype\": \"bespokesynth\",\n"
   "  \"format\": \"bespokesynth-wasm-state\",\n"
   "  \"version\": 4,\n"
   "  \"modules\": [ { \"id\": 20, \"type\": \"gain\", \"position\": { \"x\": 0, \"y\": 0 }, "
   "\"controls\": { \"gain\": 0.5 } } ],\n"
   "  \"connections\": [],\n"
   "  \"midi\": { \"note_channel\": -1, \"cc\": [] }\n"
   "}\n";
   ModuleCanvas::StateSnapshot emptyLoaded;
   TEST("Explicit empty CC map deserializes", deserializePatchState(emptyMapJson, emptyLoaded, viewMode, error));
   TEST("Explicit empty CC map stays empty", emptyLoaded.midiCcMap.empty());
}


// --- Catalog wave 2: mixer / compressor / chorus / waveshaper / clockdivider / drumseq / freeverb ---
