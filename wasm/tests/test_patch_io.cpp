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
   TEST("Migrated patch serializes current version", v2.find("\"version\": " + std::to_string(kPatchSchemaVersion)) != std::string::npos);
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

void test_unknown_type_placeholders()
{
   printf("\n=== Unknown-Type Placeholder Tests ===\n");

   // A patch from a build (or the desktop app) that has module types this one doesn't:
   // "multitrackrecorder" (desktop-only) sits in the signal path, "futurething" hangs off the side.
   const char* fixture =
   "{\n"
   "  \"filetype\": \"bespokesynth\",\n"
   "  \"format\": \"bespokesynth-wasm-state\",\n"
   "  \"version\": 4,\n"
   "  \"transport\": { \"bpm\": 120, \"playing\": false },\n"
   "  \"modules\": [\n"
   "    { \"id\": 1, \"type\": \"oscillator\", \"position\": { \"x\": 10, \"y\": 20 },\n"
   "      \"controls\": { \"frequency\": 330, \"volume\": 0.5, \"waveform\": 1 } },\n"
   "    { \"id\": 2, \"type\": \"multitrackrecorder\", \"position\": { \"x\": 200, \"y\": 40 },\n"
   "      \"controls\": { \"carrier\": 0.3, \"depth\": 0.7, \"bands\": 16 },\n"
   "      \"extras\": { \"preset\": \"robot\" } },\n"
   "    { \"id\": 3, \"type\": \"delay\", \"position\": { \"x\": 400, \"y\": 60 }, \"controls\": { \"time\": 250 } },\n"
   "    { \"id\": 4, \"type\": \"output\", \"position\": { \"x\": 600, \"y\": 80 }, \"controls\": {} },\n"
   "    { \"id\": 5, \"type\": \"futurething\", \"position\": { \"x\": 200, \"y\": 200 }, \"controls\": { \"amount\": 2 } }\n"
   "  ],\n"
   "  \"connections\": [\n"
   "    { \"source\": { \"module\": 1, \"port\": 0 }, \"destination\": { \"module\": 2, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 2, \"port\": 0 }, \"destination\": { \"module\": 3, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 1, \"port\": 0 }, \"destination\": { \"module\": 3, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 3, \"port\": 0 }, \"destination\": { \"module\": 4, \"port\": 0 } }\n"
   "  ]\n"
   "}\n";

   ModuleCanvas::StateSnapshot loaded;
   int viewMode = 0;
   std::string error;
   TEST("Fixture with unknown types parses", deserializePatchState(fixture, loaded, viewMode, error));

   const int baselineModules = ModuleCanvas().getModuleCount(); // transport + scale
   ModuleCanvas canvas;
   canvas.clearUndoHistory();
   TEST("Canvas applies a patch with unknown types", canvas.applyStateSnapshot(loaded));
   TEST("Both unknown modules become placeholders", canvas.placeholderCount() == 2);
   TEST("Known modules load normally", canvas.getModuleCount() == baselineModules + 5);

   // Loading renumbers modules, so find them by their saved type.
   const int recorderId = canvas_module_id(canvas, "multitrackrecorder");
   const int futureId = canvas_module_id(canvas, "futurething");
   const int oscId = canvas_module_id(canvas, "oscillator");
   const int delayId = canvas_module_id(canvas, "delay");
   const int outputId = canvas_module_id(canvas, "output");
   TEST("Saved types resolve to modules", recorderId > 0 && futureId > 0 && oscId > 0 && delayId > 0 && outputId > 0);

   Module* recorder = canvas.getModule(recorderId);
   TEST("Placeholder keeps its position", recorder && recorder->getX() == 200.0f && recorder->getY() == 40.0f);
   TEST("Placeholder is labelled with the original type",
        recorder && recorder->isPlaceholder() && recorder->getName() == "multitrackrecorder" && recorder->serializedType() == "multitrackrecorder");
   TEST("Placeholder is not a runtime module type", recorder && recorder->getType() == "placeholder" &&
                                                    WasmModuleAdapterRegistry::instance().find("placeholder") == nullptr);
   TEST("Placeholder has no ports (no DSP to cable)", recorder && recorder->getInputs().empty() && recorder->getOutputs().empty());
   TEST("A known module is not a placeholder", canvas.getModule(oscId) && !canvas.getModule(oscId)->isPlaceholder());

   // Never silently remapped onto another module: the graph holds only the real modules.
   const auto graph = canvas.getAudioGraphSnapshot();
   int oscillators = 0;
   bool hasPlaceholderNode = false;
   for (const auto& node : graph->nodes)
   {
      if (node.id == recorderId || node.id == futureId)
         hasPlaceholderNode = true;
      const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().adapterAt(node.typeIndex);
      if (adapter && std::string(adapter->typeId()) == "oscillator")
         ++oscillators;
   }
   TEST("Unknown types add no audio-graph node, and no oscillator stands in for them",
        !hasPlaceholderNode && oscillators == 1);
   bool directCable = false;
   for (const auto& cable : graph->connections)
      directCable = directCable || (cable.sourceModuleId == oscId && cable.destModuleId == delayId);
   TEST("Cables between known modules still connect", directCable && graph->processPlan.valid);

   // The patch saves back with its unknown modules and cables intact.
   const ModuleCanvas::StateSnapshot saved = canvas.createStateSnapshot();
   auto findModule = [](const ModuleCanvas::StateSnapshot& snapshot, int id) -> const ModuleCanvas::StateModule*
   {
      for (const auto& module : snapshot.modules)
      {
         if (module.id == id)
            return &module;
      }
      return nullptr;
   };
   const auto* savedRecorder = findModule(saved, recorderId);
   TEST("Saved state keeps the original type name", savedRecorder && savedRecorder->type == "multitrackrecorder");
   TEST("Saved state keeps the unknown module's controls and extras",
        savedRecorder && savedRecorder->controls.count("carrier") && savedRecorder->controls.at("depth") == 0.7f &&
        savedRecorder->controls.at("bands") == 16.0f && savedRecorder->extras.count("preset") &&
        savedRecorder->extras.at("preset") == "robot");
   const auto* savedFuture = findModule(saved, futureId);
   TEST("Second placeholder is saved too", savedFuture && savedFuture->type == "futurething" &&
                                           savedFuture->controls.count("amount") && savedFuture->controls.at("amount") == 2.0f);
   auto hasCable = [](const ModuleCanvas::StateSnapshot& snapshot, int src, int dst)
   {
      return std::any_of(snapshot.connections.begin(), snapshot.connections.end(),
                         [src, dst](const auto& c)
                         {
                            return c.sourceModuleId == src && c.destModuleId == dst;
                         });
   };
   TEST("Cables into and out of a placeholder are preserved",
        hasCable(saved, oscId, recorderId) && hasCable(saved, recorderId, delayId) && hasCable(saved, oscId, delayId) &&
        hasCable(saved, delayId, outputId) && saved.connections.size() == 4);

   const std::string json = serializePatchState(saved, 0);
   TEST("Serialized patch names the unknown types, never 'placeholder'",
        json.find("\"multitrackrecorder\"") != std::string::npos && json.find("\"futurething\"") != std::string::npos &&
        json.find("\"placeholder\"") == std::string::npos);
   TEST("Schema version is unchanged by placeholders", saved.schemaVersion == kPatchSchemaVersion);

   ModuleCanvas::StateSnapshot reloaded;
   TEST("Saved patch parses again", deserializePatchState(json, reloaded, viewMode, error));
   ModuleCanvas second;
   const bool secondLoaded = second.applyStateSnapshot(reloaded);
   // Loading renumbers modules, so the reloaded patch matches by type, position, values and cables.
   TEST("...and loads into a fresh canvas with the same placeholders",
        secondLoaded && second.placeholderCount() == 2 &&
        canonical_by_type(second.createStateSnapshot()) == canonical_by_type(saved));

   // The public create path still refuses unknown types: placeholders come from loading only.
   ModuleCanvas strict;
   const int before = strict.getModuleCount();
   TEST("createModule still rejects an unknown type", strict.createModule("multitrackrecorder", 0, 0) < 0 &&
                                                      strict.getModuleCount() == before);

   // Deleting a placeholder drops its cables; undo brings the module and its cables back.
   canvas.clearUndoHistory();
   canvas.deleteModule(recorderId);
   const auto afterDelete = canvas.createStateSnapshot();
   TEST("Deleting a placeholder removes it and its cables",
        canvas.placeholderCount() == 1 && !findModule(afterDelete, recorderId) && !hasCable(afterDelete, oscId, recorderId) &&
        !hasCable(afterDelete, recorderId, delayId) && hasCable(afterDelete, oscId, delayId));
   TEST("Undo restores the placeholder", canvas.undo() && canvas.placeholderCount() == 2);
   const auto afterUndo = canvas.createStateSnapshot();
   const int restoredId = canvas_module_id(canvas, "multitrackrecorder");
   const auto* restored = findModule(afterUndo, restoredId);
   TEST("Undo restores its type, controls and cables",
        restored && restored->type == "multitrackrecorder" && restored->controls.count("carrier") &&
        restored->extras.count("preset") && hasCable(afterUndo, oscId, restoredId) &&
        hasCable(afterUndo, restoredId, delayId));
   TEST("Redo removes it again", canvas.redo() && canvas.placeholderCount() == 1);

   // Replacing a patch clears the previous placeholders and their kept cables.
   ModuleCanvas::StateSnapshot plain;
   plain.modules.push_back({ 1, "oscillator", 0.0f, 0.0f, false, true, {}, {} });
   TEST("Loading a patch without unknown types clears placeholders",
        canvas.applyStateSnapshot(plain) && canvas.placeholderCount() == 0 &&
        canvas.createStateSnapshot().connections.empty());
}
