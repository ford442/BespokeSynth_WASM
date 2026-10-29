#include "test_common.h"
#include "BespokeWasm/AdapterStorage.h"
#include <cstddef>

void test_filter_module_adapter()
{
   printf("\n=== Filter Module Adapter Tests ===\n");

   const FilterModuleAdapter adapter;
   TEST("Filter adapter type id", std::string(adapter.typeId()) == "filter");
   TEST("Filter adapter audio role", adapter.audioRole() == WasmAudioRole::AudioProcessor);

   std::map<std::string, float> controls = { { "cutoff", 800.0f }, { "resonance", 0.4f }, { "type", 1.0f } };
   FilterParams params{};
   adapter.fillParams(controls, &params);
   TEST("Filter adapter fills cutoff", std::fabs(params.cutoff - 800.0f) < 0.01f);
   TEST("Filter adapter fills resonance", std::fabs(params.resonance - 0.4f) < 0.01f);
   TEST("Filter adapter fills filter type", params.type == 1);
   TEST("Filter adapter declares audio in and CV", adapter.inputPorts().size() == 2);

   FilterAdapterRuntimeState runtime{};
   adapter.initRuntimeState(&runtime);

   float buffer[64] = {};
   for (int i = 0; i < 64; ++i)
      buffer[i] = std::sinf(static_cast<float>(i) * 0.2f);

   WasmAudioProcessContext context;
   context.sampleRate = 44100.0f;
   context.numSamples = 64;
   adapter.processMono(&runtime, &params, buffer, context);
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
      TEST("Filter UI inherits adapter ports", ui->getInputs().size() == 2 && ui->getOutputs().size() == 1);
      TEST("Filter UI CV port is modulation", ui->getInputs()[1].type == PortType::Modulation);
   }
}

void test_first_wave_module_adapters()
{
   printf("\n=== First-Wave Module Adapter Tests ===\n");

   {
      NoiseModuleAdapter adapter;
      NoiseParams params{};
      adapter.fillParams({ { "volume", 0.5f }, { "color", 0.0f } }, &params);
      NoiseAdapterRuntimeState runtime{};
      adapter.initRuntimeState(&runtime);
      float buffer[256] = {};
      WasmAudioProcessContext context;
      context.sampleRate = 44100.0f;
      context.numSamples = 256;
      adapter.processMono(&runtime, &params, buffer, context);
      TEST("Noise adapter produces non-silent output", buffer_rms(std::vector<float>(buffer, buffer + 256)) > 0.01f);
      TEST("Noise adapter registered", WasmModuleAdapterRegistry::instance().find("noise") != nullptr);
   }

   {
      DelayModuleAdapter adapter;
      DelayParams params{};
      adapter.fillParams({ { "time", 0.01f }, { "feedback", 0.5f }, { "mix", 0.5f } }, &params);
      std::vector<uint8_t> runtimeStorage(adapter.runtimeStateSize());
      adapter.initRuntimeState(runtimeStorage.data());

      float buffer[512] = {};
      buffer[0] = 1.0f;
      WasmAudioProcessContext context;
      context.sampleRate = 44100.0f;
      context.numSamples = 512;
      adapter.processMono(runtimeStorage.data(), &params, buffer, context);
      double tail = 0.0;
      for (int i = 100; i < 512; ++i)
         tail += std::fabs(buffer[i]);
      TEST("Delay adapter leaves echo energy", tail > 0.01);
      TEST("Delay adapter registered", WasmModuleAdapterRegistry::instance().find("delay") != nullptr);
      adapter.destroyRuntimeState(runtimeStorage.data());
   }

   {
      AdsrModuleAdapter adapter;
      AdsrParams params{};
      adapter.fillParams(
      { { "attack", 5.0f }, { "decay", 40.0f }, { "sustain", 0.4f }, { "release", 80.0f } }, &params);
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
      adapter.processMono(&runtime, &params, buffer, context);
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

      appendAudioGraphNode(graph, 1, "noise", true, { { "volume", 0.4f } });
      appendAudioGraphNode(graph, 2, "delay", true,
                           { { "time", 0.05f }, { "feedback", 0.2f }, { "mix", 0.4f } });
      appendAudioGraphNode(graph, 3, "output", true, { { "dcblock", 0.0f } }); // a constant must pass through

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
   StepSequencerParams seqParams{};
   adapter.fillParams(
   { { "pattern", 1.0f }, { "pitch", 72.0f }, { "gate", 0.5f }, { "steps", 16.0f } }, &seqParams);
   TEST("Sequencer fills pattern mask", seqParams.patternMask == 1);
   TEST("Sequencer fills pitch", seqParams.pitch == 72);
   TEST("Sequencer declares note output", adapter.outputPorts().size() == 1 &&
                                          adapter.outputPorts()[0].type == PortType::Note);

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

   appendAudioGraphNode(graph, 1, "stepsequencer", true,
                        { { "pattern", 1.0f }, { "pitch", 60.0f }, { "gate", 0.5f }, { "steps", 16.0f } });
   appendAudioGraphNode(graph, 2, "oscillator", true, { { "volume", 1.0f }, { "frequency", 440.0f } });
   appendAudioGraphNode(graph, 3, "output", true, { { "dcblock", 0.0f } }); // exact-silence checks below

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
   constexpr int bufferSize = 512;
   compileAudioProcessPlan(graph, graph.processPlan);
   TEST("Sequencer plan valid", graph.processPlan.valid);
   TEST("Sequencer listed as note source", graph.processPlan.noteSourcePlans.size() == 1);
   bool oscNoteCable = false;
   int oscScratchCount = 0;
   for (const auto& step : graph.processPlan.steps)
   {
      if (step.moduleId == 2)
      {
         oscNoteCable = step.hasNoteCable;
         oscScratchCount = static_cast<int>(step.noteSources.count);
      }
   }
   TEST("Oscillator plan has note cable", oscNoteCable);
   TEST("Oscillator plan has note scratch slot", oscScratchCount > 0);

   {
      StepSequencerModuleAdapter seqAdapter;
      StepSequencerParams seqP{};
      seqAdapter.fillParams(
      { { "pattern", 1.0f }, { "pitch", 60.0f }, { "gate", 0.5f }, { "steps", 16.0f } }, &seqP);
      std::vector<uint8_t> seqRt(seqAdapter.runtimeStateSize());
      seqAdapter.initRuntimeState(seqRt.data());
      WasmNoteEvent emitted[64];
      int emittedCount = 0;
      seqAdapter.emitNotesForBeatRange(seqRt.data(), &seqP, 0.0, 0.05, emitted, 64, emittedCount, nullptr, 0);
      TEST("Sequencer emits notes in first 0.05 beats", emittedCount > 0);

      OscillatorModuleAdapter oscAdapter;
      std::vector<uint8_t> oscRt(oscAdapter.runtimeStateSize());
      oscAdapter.initRuntimeState(oscRt.data());
      OscillatorParams oscP{};
      oscP.volume = 1.0f;
      WasmAudioProcessContext ctx;
      ctx.sampleRate = 44100.0f;
      ctx.numSamples = 128;
      ctx.hasNoteCable = true;
      ctx.notes = emitted;
      ctx.noteCount = emittedCount;
      float buf[128] = {};
      oscAdapter.processMono(oscRt.data(), &oscP, buf, ctx);
      TEST("Oscillator sounds when gated by sequencer notes", buffer_rms(std::vector<float>(buf, buf + 128)) > 0.01f);
      oscAdapter.destroyRuntimeState(oscRt.data());
      seqAdapter.destroyRuntimeState(seqRt.data());
   }

   AudioGraphEngine engine;
   engine.prepareForBlock(bufferSize, 128, 32);
   engine.prepareRuntimeStates(graph);
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
   auto* seqParamsMut = static_cast<StepSequencerParams*>(graph.paramsFor(graph.nodes[0]));
   TEST("Sequencer params are in the arena", seqParamsMut != nullptr);
   if (seqParamsMut)
      seqParamsMut->patternMask = 0;
   compileAudioProcessPlan(graph, graph.processPlan);
   double silentEnergy = 0.0;
   for (int b = 0; b < 20; ++b)
   {
      std::vector<float> left(bufferSize, 0.0f);
      std::vector<float> right(bufferSize, 0.0f);
      float* outputs[2] = { left.data(), right.data() };
      engine.processBlock(graph, outputs, 2, bufferSize, 44100.0f);
      silentEnergy += buffer_rms(left);
   }
   TEST("Empty pattern with note cable stays silent", silentEnergy == 0.0);
}

void test_typed_adapter_round_trip()
{
   printf("\n=== Typed Adapter Round-Trip Tests ===\n");

   struct FakeConstantParams
   {
      float value = 0.25f;
   };

   class FakeConstantAdapter : public AdapterStorage<FakeConstantParams>
   {
   public:
      const char* typeId() const override { return "test.constant"; }
      const char* displayName() const override { return "Test Constant"; }
      ModuleCategory category() const override { return ModuleCategory::Synth; }
      WasmAudioRole audioRole() const override { return WasmAudioRole::AudioSource; }
      ParamTable paramTable() const override
      {
         static constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "value", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.25f, .offset = offsetof(FakeConstantParams, value) },
         };
         return kParams;
      }
      std::span<const PortDescriptor> outputPorts() const override
      {
         static constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };
         return kOutputs;
      }
      std::unique_ptr<Module> createUiModule(int) const override { return nullptr; }
      void processAudio(void*, const void* paramsPtr, float* const* channels, const WasmAudioProcessContext& context) const override
      {
         const auto& params = *static_cast<const Params*>(paramsPtr);
         for (int i = 0; i < context.numSamples; ++i)
            channels[0][i] = params.value;
      }
   };

   struct FakeScaleParams
   {
      float scale = 2.0f;
   };

   class FakeScaleAdapter : public AdapterStorage<FakeScaleParams>
   {
   public:
      const char* typeId() const override { return "test.scale"; }
      const char* displayName() const override { return "Test Scale"; }
      ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
      WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
      ParamTable paramTable() const override
      {
         static constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "scale", .minValue = 0.0f, .maxValue = 4.0f, .defaultValue = 2.0f, .offset = offsetof(FakeScaleParams, scale) },
         };
         return kParams;
      }
      std::span<const PortDescriptor> inputPorts() const override
      {
         static constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" } };
         return kInputs;
      }
      std::span<const PortDescriptor> outputPorts() const override
      {
         static constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };
         return kOutputs;
      }
      std::unique_ptr<Module> createUiModule(int) const override { return nullptr; }
      void processAudio(void*, const void* paramsPtr, float* const* channels, const WasmAudioProcessContext& context) const override
      {
         const auto& params = *static_cast<const Params*>(paramsPtr);
         for (int ch = 0; ch < context.numChannels; ++ch)
            for (int i = 0; i < context.numSamples; ++i)
               channels[ch][i] *= params.scale;
      }
   };

   auto& registry = WasmModuleAdapterRegistry::instance();
   registry.registerAdapter(std::make_unique<FakeConstantAdapter>());
   registry.registerAdapter(std::make_unique<FakeScaleAdapter>());

   TEST("Fake constant adapter registered by type id", registry.find("test.constant") != nullptr);
   TEST("Fake scale adapter interned type index", registry.typeIndexFor("test.scale") >= 0);
   TEST("adapterAt matches find", registry.adapterAt(registry.typeIndexFor("test.constant")) == registry.find("test.constant"));

   AudioGraphSnapshot graph;
   graph.transportPlaying = true;
   appendAudioGraphNode(graph, 1, "test.constant", true, { { "value", 0.25f } });
   appendAudioGraphNode(graph, 2, "test.scale", true, { { "scale", 2.0f } });
   appendAudioGraphNode(graph, 3, "output", true, { { "dcblock", 0.0f } }); // a constant must pass through

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
   TEST("Fake adapter graph is constant 0.5", std::fabs(rendered[0] - 0.5f) < 0.0001f);
   TEST("Fake adapter graph is non-silent", buffer_rms(rendered) > 0.4f);

   TEST("AudioGraphNode has no module-specific payload", sizeof(AudioGraphNode) <= 32);
}

void test_reverb_adapter()
{
   printf("\n=== Reverb Adapter Tests ===\n");

   TEST("Reverb adapter registered", WasmModuleAdapterRegistry::instance().find("reverb") != nullptr);
   ReverbModuleAdapter adapter;
   TEST("Reverb declares audio ports", adapter.inputPorts().size() == 1 && adapter.outputPorts().size() == 1);

   ReverbParams params{};
   adapter.fillParams({ { "room", 0.8f }, { "damping", 0.2f }, { "mix", 0.5f } }, &params);
   TEST("Reverb fills room size", std::fabs(params.roomSize - 0.8f) < 0.01f);

   std::vector<uint8_t> runtime(adapter.runtimeStateSize());
   adapter.initRuntimeState(runtime.data());
   float buffer[2048] = {};
   buffer[0] = 1.0f;
   WasmAudioProcessContext context;
   context.sampleRate = 44100.0f;
   context.numSamples = 2048;
   adapter.processMono(runtime.data(), &params, buffer, context);
   double tail = 0.0;
   for (int i = 1200; i < 2048; ++i)
      tail += std::fabs(buffer[i]);
   TEST("Reverb leaves tail energy", tail > 0.001);
   adapter.destroyRuntimeState(runtime.data());

   AudioGraphSnapshot graph;
   graph.transportPlaying = true;
   appendAudioGraphNode(graph, 1, "noise", true, { { "volume", 0.4f } });
   appendAudioGraphNode(graph, 2, "reverb", true, { { "room", 0.7f }, { "mix", 0.4f } });
   appendAudioGraphNode(graph, 3, "output", true, {});
   AudioGraphConnection c1{ 1, 0, 2, 0, PortType::Audio, PortType::Audio };
   AudioGraphConnection c2{ 2, 0, 3, 0, PortType::Audio, PortType::Audio };
   graph.connections.push_back(c1);
   graph.connections.push_back(c2);
   auto rendered = render_graph(graph);
   TEST("Noise→reverb→output graph is non-silent", buffer_rms(rendered) > 0.001f);
}
