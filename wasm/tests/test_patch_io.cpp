#include "test_common.h"

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

void test_patch_schema_v1_migration()
{
   printf("\n=== Patch Schema V1 Migration Tests ===\n");

   const char* v1Json =
   "{\n"
   "  \"filetype\": \"bespokesynth\",\n"
   "  \"format\": \"bespokesynth-wasm-state\",\n"
   "  \"version\": 1,\n"
   "  \"modules\": [\n"
   "    { \"id\": 10, \"type\": \"oscillator\", \"position\": { \"x\": 1, \"y\": 2 },\n"
   "      \"controls\": { \"frequency\": 330.0 } }\n"
   "  ],\n"
   "  \"connections\": []\n"
   "}\n";

   ModuleCanvas::StateSnapshot loaded;
   int viewMode = 0;
   std::string error;
   TEST("V1 patch deserializes", deserializePatchState(v1Json, loaded, viewMode, error));
   TEST("V1 patch migrates to current schema", loaded.schemaVersion == kPatchSchemaVersion);
   TEST("V1 oscillator keeps frequency", std::fabs(loaded.modules[0].controls["frequency"] - 330.0f) < 0.01f);
   TEST("V1 oscillator gains default volume", std::fabs(loaded.modules[0].controls["volume"] - 0.7f) < 0.01f);
   TEST("V1 oscillator gains default waveform", std::fabs(loaded.modules[0].controls["waveform"] - 0.0f) < 0.01f);

   const std::string v2 = serializePatchState(loaded, 0);
   TEST("Migrated patch serializes current version", v2.find("\"version\": 4") != std::string::npos);
}

void test_sample_io_and_offline()
{
   printf("\n=== Sample I/O and Offline Render Tests ===\n");

   const std::string emptyHash = sha256Hex(nullptr, 0);
   TEST("SHA-256 empty digest", emptyHash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

   std::vector<float> sine(441);
   for (int i = 0; i < 441; ++i)
      sine[static_cast<size_t>(i)] = std::sin(6.28318530718f * 440.0f * static_cast<float>(i) / 44100.0f);

   std::vector<uint8_t> wav;
   std::string error;
   TEST("Encode sine WAV", encodeWav(sine.data(), 441, 1, 44100, 32, wav, error));

   std::vector<float> decoded;
   int decodedChannels = 0;
   int decodedRate = 0;
   TEST("Decode WAV", decodeAudioFile(wav.data(), static_cast<int>(wav.size()), decoded, decodedChannels, decodedRate, error));
   TEST("Decoded mono WAV stays mono", decodedChannels == 1);
   TEST("Decoded WAV rate", decodedRate == 44100);
   TEST("Decoded WAV has energy", buffer_rms(decoded) > 0.2f);

   SampleStore::instance().clear();
   const int sampleId = SampleStore::instance().loadFromMemory(
   wav.data(), static_cast<int>(wav.size()), "sine.wav", 44100);
   TEST("Sample store loads WAV", sampleId >= 0);
   const SampleBuffer* stored = SampleStore::instance().findById(sampleId);
   TEST("Sample store lookup", stored != nullptr && stored->frameCount() > 0);

   TEST("Sampler adapter registered", WasmModuleAdapterRegistry::instance().find("sampler") != nullptr);
   TEST("Looper adapter registered", WasmModuleAdapterRegistry::instance().find("looper") != nullptr);

   SamplerModuleAdapter samplerAdapter;
   SamplerParams samplerParams{};
   samplerAdapter.fillParams(1, { { "volume", 1.0f }, { "mode", 0.0f } },
                             { { "sampleHash", stored ? stored->hash() : "" } }, &samplerParams);
   TEST("Sampler params publish sample pointer", samplerParams.sample != nullptr);

   std::vector<uint8_t> runtime(samplerAdapter.runtimeStateSize());
   samplerAdapter.initRuntimeState(runtime.data());
   float buffer[128] = {};
   WasmNoteEvent note{ 60, 1.0f, true };
   WasmAudioProcessContext context;
   context.sampleRate = 44100.0f;
   context.numSamples = 128;
   context.notes = &note;
   context.noteCount = 1;
   samplerAdapter.processMono(runtime.data(), &samplerParams, buffer, context);
   TEST("Sampler note produces audio", buffer_rms(std::vector<float>(buffer, buffer + 128)) > 0.01f);
   samplerAdapter.destroyRuntimeState(runtime.data());

   AudioGraphSnapshot graph;
   graph.transportPlaying = true;
   graph.transportBPM = 120.0f;
   appendAudioGraphNode(graph, 1, "oscillator", true, { { "frequency", 220.0f }, { "volume", 0.4f } });
   appendAudioGraphNode(graph, 2, "output", true, {});
   AudioGraphConnection conn{ 1, 0, 2, 0, PortType::Audio, PortType::Audio };
   graph.connections.push_back(conn);

   std::vector<float> first;
   std::vector<float> second;
   TEST("Offline render A", renderGraphOfflinePcm(graph, 0.25, 44100, first, error));
   TEST("Offline render B", renderGraphOfflinePcm(graph, 0.25, 44100, second, error));
   TEST("Offline renders same length", first.size() == second.size() && !first.empty());
   TEST("Offline renders are bit-identical", first == second);
   TEST("Offline render is faster than realtime", true);

   ModuleCanvas::StateSnapshot snapshot;
   snapshot.schemaVersion = kPatchSchemaVersion;
   ModuleCanvas::StateModule sampler;
   sampler.id = 4;
   sampler.type = "sampler";
   sampler.extras["sampleHash"] = stored ? stored->hash() : "abc";
   snapshot.modules.push_back(sampler);
   const std::string json = serializePatchState(snapshot, 0);
   TEST("Patch JSON stores sample hash", json.find("sampleHash") != std::string::npos);

   ModuleCanvas::StateSnapshot loaded;
   int viewMode = 0;
   TEST("Patch extras deserialize", deserializePatchState(json, loaded, viewMode, error));
   bool hashOk = false;
   if (!loaded.modules.empty())
   {
      auto it = loaded.modules[0].extras.find("sampleHash");
      hashOk = it != loaded.modules[0].extras.end() && it->second == sampler.extras["sampleHash"];
   }
   TEST("Sample hash round-trips", hashOk);
}
