#include "test_common.h"

// --- Catalog wave 2: mixer / compressor / chorus / waveshaper / clockdivider / drumseq / freeverb ---

void test_catalog_wave2_registration()
{
   printf("\n=== Catalog Wave 2: Registration ===\n");
   auto& registry = WasmModuleAdapterRegistry::instance();
   struct Expect
   {
      const char* type;
      ModuleCategory category;
      WasmAudioRole role;
   };
   const Expect expected[] = {
      { "mixer", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "compressor", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "chorus", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "waveshaper", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "clockdivider", ModuleCategory::Pulse, WasmAudioRole::PulseSource },
      { "drumseq", ModuleCategory::Pulse, WasmAudioRole::NoteSource },
   };
   for (const auto& e : expected)
   {
      const WasmModuleAdapter* adapter = registry.find(e.type);
      char name[96];
      snprintf(name, sizeof(name), "%s registered with category + role", e.type);
      TEST(name, adapter && adapter->category() == e.category && adapter->audioRole() == e.role);

      bool listed = false;
      for (const auto& info : registry.typesByCategory(e.category))
         listed |= info.type == e.type;
      snprintf(name, sizeof(name), "%s listed in spawn category", e.type);
      TEST(name, listed);

      auto ui = registry.createModule(e.type, 900);
      snprintf(name, sizeof(name), "%s UI module inherits adapter ports", e.type);
      TEST(name, ui && adapter && ui->getInputs().size() == adapter->inputPorts().size() &&
                 ui->getOutputs().size() == adapter->outputPorts().size());
      if (ui && adapter)
      {
         bool controlsRoundTrip = true;
         for (const auto& control : adapter->controlDescriptors())
            controlsRoundTrip &= std::fabs(ui->getControlValue(control.name) - control.defaultValue) < 1e-4f;
         snprintf(name, sizeof(name), "%s UI defaults match control descriptors", e.type);
         TEST(name, controlsRoundTrip);
      }
   }

   auto mixerUi = registry.createModule("mixer", 901);
   mixerUi->setControlValue("level2", 5.0f);
   TEST("Slider panel clamps to spec range", std::fabs(mixerUi->getControlValue("level2") - 1.0f) < 1e-6f);
   auto dividerUi = registry.createModule("clockdivider", 902);
   dividerUi->setControlValue("divide", 3.4f);
   TEST("Slider panel snaps integer controls", std::fabs(dividerUi->getControlValue("divide") - 3.0f) < 1e-6f);
   TEST("DrumSeq opts out of Scale quantization", !registry.find("drumseq")->quantizesNotesToScale());
}

void test_catalog_wave2_mixer_ports()
{
   printf("\n=== Catalog Wave 2: Mixer (per-port audio inputs) ===\n");
   auto build = [](float level3, bool secondSource)
   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      std::map<int, std::string> types = { { 1, "oscillator" }, { 2, "oscillator" }, { 3, "mixer" }, { 4, "output" } };
      appendAudioGraphNode(graph, 1, "oscillator", true, { { "frequency", 220.0f }, { "volume", 0.5f } });
      appendAudioGraphNode(graph, 2, "oscillator", true,
                           { { "frequency", 330.0f }, { "volume", 0.5f }, { "waveform", 1.0f } });
      appendAudioGraphNode(graph, 3, "mixer", true,
                           { { "level1", 1.0f }, { "level2", 1.0f }, { "level3", level3 }, { "master", 1.0f } });
      appendAudioGraphNode(graph, 4, "output", true, {});
      graph.connections.push_back(typed_connection(1, 0, 3, 0));
      if (secondSource)
         graph.connections.push_back(typed_connection(2, 0, 3, 2));
      graph.connections.push_back(typed_connection(3, 0, 4, 0));
      resolve_port_types(graph, types);
      return graph;
   };

   AudioGraphSnapshot plan = build(1.0f, true);
   compileAudioProcessPlan(plan, plan.processPlan);
   bool portsTagged = false;
   for (const auto& step : plan.processPlan.steps)
   {
      if (step.moduleId != 3)
         continue;
      portsTagged = step.inputBufferSlots.size() == 2 && step.inputBufferPorts.size() == 2 &&
                    step.inputBufferPorts.data[0] == 0 && step.inputBufferPorts.data[1] == 2;
   }
   TEST("Plan tags mixer inputs with destination ports", portsTagged);

   AudioGraphSnapshot solo = build(1.0f, false);
   AudioGraphSnapshot mutedCh3 = build(0.0f, true);
   AudioGraphSnapshot both = build(1.0f, true);
   auto soloOut = render_blocks(solo, 4);
   auto mutedOut = render_blocks(mutedCh3, 4);
   auto bothOut = render_blocks(both, 4);
   double mutedDiff = 0.0;
   double bothDiff = 0.0;
   for (size_t i = 0; i < soloOut.size(); ++i)
   {
      mutedDiff += std::fabs(soloOut[i] - mutedOut[i]);
      bothDiff += std::fabs(soloOut[i] - bothOut[i]);
   }
   TEST("Mixer channel level applies per port (ch3 at 0 == ch1 alone)", mutedDiff < 1e-3);
   TEST("Mixer sums a second port when its level is up", bothDiff > 1.0);
   TEST("Mixer output is non-silent", buffer_rms(soloOut) > 0.05f);
}

void test_catalog_wave2_dynamics_and_colour()
{
   printf("\n=== Catalog Wave 2: Compressor / Chorus / Waveshaper ===\n");

   CompressorParams curve;
   curve.thresholdDb = -20.0f;
   curve.ratio = 4.0f;
   curve.kneeDb = 0.0f;
   TEST("Compressor below threshold is unity", CompressorModuleAdapter::gainComputerDb(-30.0f, curve) == 0.0f);
   TEST("Compressor 4:1 turns 20 dB over into -15 dB",
        std::fabs(CompressorModuleAdapter::gainComputerDb(0.0f, curve) + 15.0f) < 1e-4f);
   curve.kneeDb = 6.0f;
   const float kneeMid = CompressorModuleAdapter::gainComputerDb(-20.0f, curve);
   TEST("Compressor soft knee is gentle at threshold", kneeMid < 0.0f && kneeMid > -2.0f);

   auto compressGraph = [](float threshold, bool sidechain, float oscVolume = 0.9f)
   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      std::map<int, std::string> types = { { 1, "oscillator" }, { 2, "noise" }, { 3, "compressor" }, { 4, "output" } };
      appendAudioGraphNode(graph, 1, "oscillator", true, { { "frequency", 220.0f }, { "volume", oscVolume } });
      appendAudioGraphNode(graph, 2, "noise", true, { { "volume", 1.0f } });
      appendAudioGraphNode(graph, 3, "compressor", true,
                           { { "threshold", threshold }, { "ratio", 10.0f }, { "attack", 1.0f }, { "release", 50.0f } });
      appendAudioGraphNode(graph, 4, "output", true, {});
      graph.connections.push_back(typed_connection(1, 0, 3, 0));
      if (sidechain)
         graph.connections.push_back(typed_connection(2, 0, 3, 1));
      graph.connections.push_back(typed_connection(3, 0, 4, 0));
      resolve_port_types(graph, types);
      return graph;
   };
   AudioGraphSnapshot open = compressGraph(0.0f, false);
   AudioGraphSnapshot squashed = compressGraph(-30.0f, false);
   auto openOut = render_blocks(open, 8);
   auto squashedOut = render_blocks(squashed, 8);
   const std::vector<float> openTail(openOut.end() - 2048, openOut.end());
   const std::vector<float> squashedTail(squashedOut.end() - 2048, squashedOut.end());
   TEST("Compressor reduces a loud sine", buffer_rms(squashedTail) < buffer_rms(openTail) * 0.5f);
   TEST("Compressor output stays finite", all_finite(squashedOut));

   // Quiet sine (-26 dB peak) sits under a -20 dB threshold; only the loud noise key can duck it.
   AudioGraphSnapshot keyed = compressGraph(-20.0f, true, 0.05f);
   AudioGraphSnapshot unkeyed = compressGraph(-20.0f, false, 0.05f);
   auto keyedOut = render_blocks(keyed, 8);
   auto unkeyedOut = render_blocks(unkeyed, 8);
   const std::vector<float> keyedTail(keyedOut.end() - 2048, keyedOut.end());
   const std::vector<float> unkeyedTail(unkeyedOut.end() - 2048, unkeyedOut.end());
   TEST("Compressor sidechain port drives the detector", buffer_rms(keyedTail) < buffer_rms(unkeyedTail) * 0.5f);
   TEST("Compressor sidechain is not mixed into the output", buffer_rms(keyedTail) < buffer_rms(unkeyedTail));

   ChorusModuleAdapter chorus;
   std::vector<uint8_t> chorusState(chorus.runtimeStateSize());
   chorus.initRuntimeState(chorusState.data());
   ChorusParams chorusParams;
   chorus.fillParams({ { "mix", 1.0f }, { "depth", 1.0f }, { "rate", 2.0f } }, &chorusParams);
   std::vector<float> dry(4096);
   for (size_t i = 0; i < dry.size(); ++i)
      dry[i] = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f) * 0.5f;
   std::vector<float> wet = dry;
   WasmAudioProcessContext chorusContext;
   chorusContext.sampleRate = 44100.0f;
   chorusContext.numSamples = static_cast<int>(wet.size());
   chorus.processAudio(chorusState.data(), &chorusParams, wet.data(), chorusContext);
   double chorusDiff = 0.0;
   for (size_t i = 0; i < wet.size(); ++i)
      chorusDiff += std::fabs(wet[i] - dry[i]);
   TEST("Chorus changes the signal", chorusDiff > 10.0);
   TEST("Chorus output is finite and non-silent", all_finite(wet) && buffer_rms(wet) > 0.05f);
   chorus.destroyRuntimeState(chorusState.data());

   WaveshaperModuleAdapter shaper;
   TEST("Waveshaper hard clip clamps", WaveshaperModuleAdapter::shape(3.0f, kWaveshaperHard) == 1.0f);
   TEST("Waveshaper fold reflects at +/-1 and is odd",
        std::fabs(WaveshaperModuleAdapter::shape(1.5f, kWaveshaperFold) - 0.5f) < 1e-5f &&
        std::fabs(WaveshaperModuleAdapter::shape(-1.5f, kWaveshaperFold) + 0.5f) < 1e-5f &&
        std::fabs(WaveshaperModuleAdapter::shape(3.0f, kWaveshaperFold) + 1.0f) < 1e-5f);
   TEST("Waveshaper soft is odd and bounded",
        WaveshaperModuleAdapter::shape(100.0f, kWaveshaperSoft) < 1.0f &&
        std::fabs(WaveshaperModuleAdapter::shape(-0.5f, kWaveshaperSoft) + WaveshaperModuleAdapter::shape(0.5f, kWaveshaperSoft)) < 1e-6f);
   WaveshaperParams shaperParams;
   shaper.fillParams({ { "drive", 10.0f }, { "mode", 1.0f }, { "mix", 1.0f }, { "level", 1.0f } }, &shaperParams);
   std::vector<float> driven = dry;
   WasmAudioProcessContext shaperContext;
   shaperContext.numSamples = static_cast<int>(driven.size());
   shaper.processAudio(nullptr, &shaperParams, driven.data(), shaperContext);
   float peak = 0.0f;
   for (float v : driven)
      peak = std::max(peak, std::fabs(v));
   TEST("Waveshaper tanh drive saturates below 1", peak <= 1.0f && buffer_rms(driven) > buffer_rms(dry));
   std::vector<float> cv(dry.size(), -1.0f);
   std::vector<float> cvDriven = dry;
   shaperContext.modulationBuffer = cv.data();
   shaper.processAudio(nullptr, &shaperParams, cvDriven.data(), shaperContext);
   TEST("Waveshaper Drive CV at -1 silences the shaped path", buffer_rms(cvDriven) < 1e-6f);
}

void test_catalog_wave2_pulse_and_drums()
{
   printf("\n=== Catalog Wave 2: ClockDivider / DrumSeq ===\n");

   ClockDividerModuleAdapter divider;
   ClockDividerParams dividerParams;
   divider.fillParams({ { "divide", 4.0f }, { "offset", 1.0f } }, &dividerParams);
   std::vector<uint8_t> dividerState(divider.runtimeStateSize());
   divider.initRuntimeState(dividerState.data());
   WasmPulseEvent in[16];
   for (int i = 0; i < 16; ++i)
      in[i] = { i * 0.25, 1.0f };
   WasmPulseEvent out[16];
   int outCount = 0;
   divider.emitPulsesForBeatRange(dividerState.data(), &dividerParams, 0.0, 4.0, out, 16, outCount, in, 16);
   TEST("ClockDivider /4 passes every fourth pulse", outCount == 4);
   TEST("ClockDivider offset shifts the phase", outCount == 4 && std::fabs(out[0].timeBeats - 0.25) < 1e-9);
   divider.emitPulsesForBeatRange(dividerState.data(), &dividerParams, 4.0, 8.0, out, 16, outCount, nullptr, 0);
   TEST("ClockDivider without input emits nothing", outCount == 0);

   // Divider is appended before the transport: the plan must still evaluate transport first.
   auto dividedGraph = [](bool useDivider)
   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      graph.transportBPM = 120.0f;
      std::map<int, std::string> types = {
         { 1, "clockdivider" }, { 2, "transport" }, { 3, "pulsetonote" }, { 4, "oscillator" }, { 5, "output" }
      };
      appendAudioGraphNode(graph, 1, "clockdivider", true, { { "divide", 4.0f } });
      appendAudioGraphNode(graph, 2, "transport", true, {});
      appendAudioGraphNode(graph, 3, "pulsetonote", true, { { "pitch", 60.0f }, { "gate", 0.75f } });
      appendAudioGraphNode(graph, 4, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(graph, 5, "output", true, {});
      if (useDivider)
      {
         graph.connections.push_back(typed_connection(2, 0, 1, 0));
         graph.connections.push_back(typed_connection(1, 0, 3, 0));
      }
      else
      {
         graph.connections.push_back(typed_connection(2, 0, 3, 0));
      }
      graph.connections.push_back(typed_connection(3, 0, 4, 0));
      graph.connections.push_back(typed_connection(4, 0, 5, 0));
      resolve_port_types(graph, types);
      return graph;
   };
   AudioGraphSnapshot divided = dividedGraph(true);
   compileAudioProcessPlan(divided, divided.processPlan);
   const auto& order = divided.processPlan.pulseSourceNodeIndices;
   TEST("Plan orders transport before clockdivider",
        order.size() == 2 && divided.nodes[static_cast<size_t>(order[0])].id == 2 &&
        divided.nodes[static_cast<size_t>(order[1])].id == 1);
   TEST("ClockDivider plan reads transport scratch",
        divided.processPlan.pulseSourcePlans.size() == 2 &&
        divided.processPlan.pulseSourcePlans[1].pulseSourceScratchSlots.size() == 1 &&
        divided.processPlan.pulseSourcePlans[1].pulseSourceScratchSlots.data[0] == 0);

   // 150 blocks × 512 @ 44.1k ≈ 1.74 s ≈ 3.5 beats at 120 BPM.
   const int dividedOnsets = count_onsets(render_blocks(divided, 150));
   AudioGraphSnapshot undivided = dividedGraph(false);
   const int undividedOnsets = count_onsets(render_blocks(undivided, 150));
   TEST("Transport→ClockDivider(/4)→PulseToNote plays quarter notes", dividedOnsets == 4);
   TEST("Undivided transport plays sixteenths", undividedOnsets >= 12);

   // DrumSeq: four-on-the-floor from Pulse, per-track Note outputs.
   auto drumGraph = [](int oscPort, int kickMask, int hatMask)
   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      graph.transportBPM = 120.0f;
      graph.musicScale = { 2, kScaleModeMajor };
      std::map<int, std::string> types = { { 1, "transport" }, { 2, "drumseq" }, { 3, "oscillator" }, { 4, "output" } };
      appendAudioGraphNode(graph, 1, "transport", true, {});
      appendAudioGraphNode(graph, 2, "drumseq", true,
                           { { "kick", static_cast<float>(kickMask) },
                             { "snare", 0.0f },
                             { "hat", static_cast<float>(hatMask) },
                             { "tom", 0.0f },
                             { "gate", 0.5f } });
      appendAudioGraphNode(graph, 3, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(graph, 4, "output", true, {});
      graph.connections.push_back(typed_connection(1, 0, 2, 0));
      graph.connections.push_back(typed_connection(2, oscPort, 3, 0));
      graph.connections.push_back(typed_connection(3, 0, 4, 0));
      resolve_port_types(graph, types);
      return graph;
   };
   AudioGraphSnapshot kickOnly = drumGraph(0, 0x1111, 0x0000);
   compileAudioProcessPlan(kickOnly, kickOnly.processPlan);
   bool kickFilter = false;
   for (const auto& step : kickOnly.processPlan.steps)
   {
      if (step.moduleId == 3)
         kickFilter = step.noteSourcePortFilters.size() == 1 && step.noteSourcePortFilters.data[0] == 0;
   }
   TEST("Plan filters drumseq Kick cable to note port 0", kickFilter);
   TEST("DrumSeq kick 0x1111 gives four-on-the-floor", count_onsets(render_blocks(kickOnly, 150)) == 4);

   AudioGraphSnapshot hatCable = drumGraph(2, 0x1111, 0x0000);
   TEST("Kick notes do not leak onto the Hat output", buffer_rms(render_blocks(hatCable, 150)) < 1e-6f);
   AudioGraphSnapshot hatPlays = drumGraph(2, 0x0000, 0x4444);
   TEST("Hat output plays its own track", count_onsets(render_blocks(hatPlays, 150)) >= 3);

   DrumSeqModuleAdapter drums;
   DrumSeqParams drumParams;
   drums.fillParams({ { "kick", 1.0f }, { "kickpitch", 37.0f } }, &drumParams);
   std::vector<uint8_t> drumState(drums.runtimeStateSize());
   drums.initRuntimeState(drumState.data());
   WasmNoteEvent notes[16];
   int noteCount = 0;
   WasmPulseEvent pulse{ 0.0, 0.8f };
   drums.emitNotesForBeatRange(drumState.data(), &drumParams, 0.0, 0.01, notes, 16, noteCount, &pulse, 1);
   TEST("DrumSeq emits kick note-on at pulse with pitch + velocity + port",
        noteCount == 1 && notes[0].isNoteOn && notes[0].pitch == 37 && notes[0].outputPort == 0 &&
        std::fabs(notes[0].velocity - 0.8f) < 1e-6f);
   drums.emitNotesForBeatRange(drumState.data(), &drumParams, 0.01, 0.2, notes, 16, noteCount, nullptr, 0);
   TEST("DrumSeq releases after gate", noteCount == 1 && !notes[0].isNoteOn && notes[0].pitch == 37);
   drums.emitNotesForBeatRange(drumState.data(), &drumParams, 0.2, 1.0, notes, 16, noteCount, nullptr, 0);
   TEST("DrumSeq without pulses stays silent", noteCount == 0);
   drums.destroyRuntimeState(drumState.data());

   auto drumUi = WasmModuleAdapterRegistry::instance().createModule("drumseq", 903);
   auto* drumModule = dynamic_cast<DrumSeqModule*>(drumUi.get());
   TEST("DrumSeq UI created", drumModule != nullptr);
   if (drumModule)
   {
      drumModule->setStepActive(3, 5, true);
      TEST("DrumSeq UI toggles steps into the tom mask",
           static_cast<int>(drumModule->getControlValue("tom")) == (1 << 5));
   }
}

void test_catalog_wave2_freeverb()
{
   printf("\n=== Catalog Wave 2: Freeverb ===\n");
   ReverbModuleAdapter adapter;
   ReverbParams params{};
   adapter.fillParams({ { "room", 0.7f }, { "damping", 0.3f }, { "mix", 1.0f } }, &params);

   // Shortest Freeverb comb is 1116 samples: wet output only starts after that.
   std::vector<float> noise(8192);
   uint32_t seed = 12345u;
   for (float& v : noise)
   {
      seed = seed * 1664525u + 1013904223u;
      v = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * 0.5f;
   }

   auto runtimeA = std::make_unique<std::vector<uint8_t>>(adapter.runtimeStateSize());
   adapter.initRuntimeState(runtimeA->data());
   WasmAudioProcessContext context;
   context.sampleRate = 44100.0f;
   context.numSamples = static_cast<int>(noise.size());
   std::vector<float> first = noise;
   adapter.processAudio(runtimeA->data(), &params, first.data(), context);
   const float inRms = buffer_rms(noise);
   const float wetRms = buffer_rms(std::vector<float>(first.begin() + 4096, first.end()));
   printf("  freeverb in rms %.4f, 100%% wet rms %.4f\n", inRms, wetRms);
   TEST("Freeverb wet level is in a sane range of the input", wetRms > inRms * 0.25f && wetRms < inRms * 2.0f);

   // Simulate the engine's runtime arena growing: bytes move, old storage is freed.
   std::vector<uint8_t> runtimeB(*runtimeA);
   runtimeA.reset();
   std::vector<float> second = noise;
   adapter.processAudio(runtimeB.data(), &params, second.data(), context);
   TEST("Freeverb survives runtime-arena relocation",
        all_finite(second) && buffer_rms(std::vector<float>(second.begin() + 4096, second.end())) > inRms * 0.25f);
   adapter.destroyRuntimeState(runtimeB.data());
}

void test_catalog_wave2_saved_state_fixture()
{
   printf("\n=== Catalog Wave 2: Saved-State Fixture ===\n");
   // Fixture: transport → clockdivider → drumseq → osc/noise+adsr → mixer → compressor → chorus →
   // waveshaper → reverb → output. Mirrors ?renderTest=2 plus the colour FX.
   const char* fixture =
   "{\n"
   "  \"filetype\": \"bespokesynth\",\n"
   "  \"format\": \"bespokesynth-wasm-state\",\n"
   "  \"version\": 4,\n"
   "  \"transport\": { \"bpm\": 120, \"playing\": true },\n"
   "  \"modules\": [\n"
   "    { \"id\": 1, \"type\": \"transport\", \"position\": { \"x\": 0, \"y\": 0 }, \"controls\": { \"bpm\": 120 } },\n"
   "    { \"id\": 2, \"type\": \"clockdivider\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"divide\": 1, \"offset\": 0 } },\n"
   "    { \"id\": 3, \"type\": \"drumseq\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"kick\": 4369, \"snare\": 4112, \"hat\": 17476, \"tom\": 0, \"kickpitch\": 33,\n"
   "                      \"snarepitch\": 38, \"hatpitch\": 90, \"tompitch\": 45, \"steps\": 16, \"gate\": 0.4 } },\n"
   "    { \"id\": 4, \"type\": \"oscillator\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"frequency\": 55, \"volume\": 0.9, \"waveform\": 0 } },\n"
   "    { \"id\": 5, \"type\": \"noise\", \"position\": { \"x\": 0, \"y\": 0 }, \"controls\": { \"volume\": 0.5 } },\n"
   "    { \"id\": 6, \"type\": \"adsr\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"attack\": 1, \"decay\": 40, \"sustain\": 0, \"release\": 20 } },\n"
   "    { \"id\": 7, \"type\": \"mixer\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"level1\": 0.9, \"level2\": 0.6, \"level3\": 0.8, \"level4\": 0.8, \"master\": 0.9 } },\n"
   "    { \"id\": 8, \"type\": \"compressor\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"threshold\": -18, \"ratio\": 4, \"attack\": 5, \"release\": 120, \"makeup\": 3, \"knee\": 6 } },\n"
   "    { \"id\": 9, \"type\": \"chorus\", \"position\": { \"x\": 0, \"y\": 0 }, \"controls\": { \"mix\": 0.3 } },\n"
   "    { \"id\": 10, \"type\": \"waveshaper\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"drive\": 1.5, \"mode\": 0, \"mix\": 0.5, \"level\": 0.8 } },\n"
   "    { \"id\": 11, \"type\": \"reverb\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"room\": 0.5, \"damping\": 0.5, \"mix\": 0.2 } },\n"
   "    { \"id\": 12, \"type\": \"output\", \"position\": { \"x\": 0, \"y\": 0 }, \"controls\": {} }\n"
   "  ],\n"
   "  \"connections\": [\n"
   "    { \"source\": { \"module\": 1, \"port\": 0 }, \"destination\": { \"module\": 2, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 2, \"port\": 0 }, \"destination\": { \"module\": 3, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 3, \"port\": 0 }, \"destination\": { \"module\": 4, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 3, \"port\": 2 }, \"destination\": { \"module\": 6, \"port\": 1 } },\n"
   "    { \"source\": { \"module\": 5, \"port\": 0 }, \"destination\": { \"module\": 6, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 4, \"port\": 0 }, \"destination\": { \"module\": 7, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 6, \"port\": 0 }, \"destination\": { \"module\": 7, \"port\": 1 } },\n"
   "    { \"source\": { \"module\": 7, \"port\": 0 }, \"destination\": { \"module\": 8, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 8, \"port\": 0 }, \"destination\": { \"module\": 9, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 9, \"port\": 0 }, \"destination\": { \"module\": 10, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 10, \"port\": 0 }, \"destination\": { \"module\": 11, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 11, \"port\": 0 }, \"destination\": { \"module\": 12, \"port\": 0 } }\n"
   "  ]\n"
   "}\n";

   ModuleCanvas::StateSnapshot loaded;
   int viewMode = 0;
   std::string error;
   TEST("Wave-2 fixture deserializes", deserializePatchState(fixture, loaded, viewMode, error));
   TEST("Wave-2 fixture keeps all modules", loaded.modules.size() == 12);
   TEST("Wave-2 fixture keeps all cables", loaded.connections.size() == 12);

   const std::string reserialized = serializePatchState(loaded, 0);
   ModuleCanvas::StateSnapshot roundTrip;
   TEST("Wave-2 fixture re-serializes", deserializePatchState(reserialized, roundTrip, viewMode, error));
   bool controlsKept = roundTrip.modules.size() == loaded.modules.size();
   for (size_t i = 0; controlsKept && i < loaded.modules.size(); ++i)
      controlsKept = loaded.modules[i].type == roundTrip.modules[i].type &&
                     loaded.modules[i].controls == roundTrip.modules[i].controls;
   TEST("Wave-2 fixture controls round-trip exactly", controlsKept);
   bool portsKept = roundTrip.connections.size() == loaded.connections.size();
   for (size_t i = 0; portsKept && i < loaded.connections.size(); ++i)
      portsKept = loaded.connections[i].sourcePortIndex == roundTrip.connections[i].sourcePortIndex &&
                  loaded.connections[i].destPortIndex == roundTrip.connections[i].destPortIndex;
   TEST("Wave-2 fixture cable ports round-trip", portsKept);

   AudioGraphSnapshot graph;
   graph.transportPlaying = true;
   graph.transportBPM = 120.0f;
   std::map<int, std::string> types;
   for (const auto& module : loaded.modules)
   {
      types[module.id] = module.type;
      appendAudioGraphNode(graph, module.id, module.type, true, module.controls, module.extras);
   }
   for (const auto& conn : loaded.connections)
      graph.connections.push_back(typed_connection(conn.sourceModuleId, conn.sourcePortIndex,
                                                   conn.destModuleId, conn.destPortIndex));
   resolve_port_types(graph, types);
   auto rendered = render_blocks(graph, 150);
   TEST("Wave-2 fixture graph plays without MIDI", buffer_rms(rendered) > 0.01f);
   TEST("Wave-2 fixture graph output is finite", all_finite(rendered));
}
