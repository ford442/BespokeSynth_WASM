// Wave 3: Note processors, Input, EQ, graph notes to MIDI out, the bundled drum kit.
// Unknown-type placeholders are covered next to the patch IO tests (test_patch_io.cpp).
#include "test_common.h"
#include "BespokeWasm/BundledKit.h"
#include "BespokeWasm/InputAudioBus.h"
#include "BespokeWasm/LooperArena.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include <cstdio>
#include <set>

namespace
{
   WasmNoteEvent ev(int pitch, float velocity, bool on, int channel = 0)
   {
      WasmNoteEvent e;
      e.pitch = pitch;
      e.velocity = velocity;
      e.isNoteOn = on;
      e.channel = channel;
      return e;
   }

   /** One processNotes call over [beatStart, beatEnd) with the given input events. */
   std::vector<WasmNoteEvent> runNotes(const WasmModuleAdapter& adapter, void* runtime, const void* params,
                                       const std::vector<WasmNoteEvent>& in, double beatStart = 0.0,
                                       double beatEnd = 0.05, int maxNotes = 64)
   {
      WasmNoteProcessBlock block;
      block.beatStart = beatStart;
      block.beatEnd = beatEnd;
      block.notes = in.empty() ? nullptr : in.data();
      block.noteCount = static_cast<int>(in.size());
      std::vector<WasmNoteEvent> out(static_cast<size_t>(maxNotes));
      int count = 0;
      adapter.processNotes(runtime, params, block, out.data(), maxNotes, count);
      out.resize(static_cast<size_t>(count));
      return out;
   }

   std::vector<int> pitchesOf(const std::vector<WasmNoteEvent>& events, bool on)
   {
      std::vector<int> pitches;
      for (const auto& e : events)
      {
         if (e.isNoteOn == on)
            pitches.push_back(e.pitch);
      }
      return pitches;
   }

   /** Adapter + params + runtime state bundle for the direct processNotes tests. */
   template <typename Adapter, typename Params>
   struct Processor
   {
      Adapter adapter;
      Params params{};
      std::vector<uint8_t> runtime;

      explicit Processor(const WasmControlMap& controls)
      {
         adapter.fillParams(controls, &params);
         runtime.resize(adapter.runtimeStateSize());
         adapter.initRuntimeState(runtime.data());
      }
      ~Processor() { adapter.destroyRuntimeState(runtime.data()); }

      std::vector<WasmNoteEvent> run(const std::vector<WasmNoteEvent>& in, double b0 = 0.0, double b1 = 0.05,
                                     int maxNotes = 64)
      {
         return runNotes(adapter, runtime.data(), &params, in, b0, b1, maxNotes);
      }
   };

   AudioGraphConnection noteCable(int src, int srcPort, int dst)
   {
      return { src, srcPort, dst, 0, PortType::Note, PortType::Note };
   }

   AudioGraphConnection audioCable(int src, int dst, int dstPort = 0)
   {
      return { src, 0, dst, dstPort, PortType::Audio, PortType::Audio };
   }

   AudioGraphConnection pulseCable(int src, int dst)
   {
      return { src, 0, dst, 0, PortType::Pulse, PortType::Pulse };
   }

   /** A snapshot plus an engine with its MIDI-out capture drained after every block. */
   struct Rig
   {
      AudioGraphSnapshot graph;
      AudioGraphEngine engine;
      std::vector<AudioGraphEngine::MidiOutNote> midi;
      std::vector<float> left = std::vector<float>(512);
      std::vector<float> right = std::vector<float>(512);

      Rig() { graph.transportPlaying = true; }

      void prepare(bool capture = true)
      {
         compileAudioProcessPlan(graph, graph.processPlan);
         engine.prepareForBlock(512, 128, 32);
         engine.prepareRuntimeStates(graph);
         engine.setMidiOutCapture(capture);
      }

      void queue(int pitch, float velocity, bool on)
      {
         AudioGraphEngine::NoteMessage note;
         note.pitch = pitch;
         note.velocity = velocity;
         note.isNoteOn = on;
         engine.queueNote(note);
      }

      /** Runs `blocks` blocks; returns the largest block RMS (left). */
      float run(int blocks)
      {
         float loudest = 0.0f;
         for (int b = 0; b < blocks; ++b)
         {
            float* outputs[2] = { left.data(), right.data() };
            // The audio-thread guard turns any allocation in the block into a trap.
            gAudioCallbackActive.store(true, std::memory_order_release);
            engine.processBlock(graph, outputs, 2, 512, 44100.0f);
            gAudioCallbackActive.store(false, std::memory_order_release);
            loudest = std::max(loudest, buffer_rms(left));
            AudioGraphEngine::MidiOutNote drained[64];
            int got;
            while ((got = engine.drainMidiOut(drained, 64)) > 0)
               midi.insert(midi.end(), drained, drained + got);
         }
         return loudest;
      }

      std::vector<int> midiPitches(bool on) const
      {
         std::vector<int> pitches;
         for (const auto& n : midi)
         {
            if (n.isNoteOn == on)
               pitches.push_back(n.pitch);
         }
         return pitches;
      }
   };

   int planSlotOf(const AudioGraphSnapshot& graph, int moduleId)
   {
      const auto& indices = graph.processPlan.noteSourceNodeIndices;
      for (size_t i = 0; i < indices.size(); ++i)
      {
         if (graph.nodes[static_cast<size_t>(indices[i])].id == moduleId)
            return static_cast<int>(i);
      }
      return -1;
   }

   std::vector<float> sine(int frames, float freq, float amplitude, float sampleRate = 44100.0f)
   {
      std::vector<float> out(static_cast<size_t>(frames));
      for (int i = 0; i < frames; ++i)
         out[static_cast<size_t>(i)] = amplitude * std::sin(2.0f * 3.14159265f * freq * static_cast<float>(i) / sampleRate);
      return out;
   }

   /** RMS of the second half of a stereo-in-place EQ run on a sine (filters have settled by then). */
   float eqGainAt(const WasmControlMap& controls, float freq)
   {
      EqModuleAdapter adapter;
      EqParams params{};
      adapter.fillParams(controls, &params);
      std::vector<uint8_t> runtime(adapter.runtimeStateSize());
      adapter.initRuntimeState(runtime.data());

      constexpr int kBlock = 512;
      constexpr int kBlocks = 16;
      const std::vector<float> input = sine(kBlock * kBlocks, freq, 0.5f);
      std::vector<float> settled;
      for (int b = 0; b < kBlocks; ++b)
      {
         std::vector<float> l(input.begin() + b * kBlock, input.begin() + (b + 1) * kBlock);
         std::vector<float> r = l;
         float* channels[2] = { l.data(), r.data() };
         WasmAudioProcessContext ctx;
         ctx.numSamples = kBlock;
         ctx.numChannels = 2;
         ctx.inputChannels = 2;
         adapter.processAudio(runtime.data(), &params, channels, ctx);
         if (b >= kBlocks / 2)
            settled.insert(settled.end(), l.begin(), l.end());
      }
      adapter.destroyRuntimeState(runtime.data());
      const std::vector<float> reference(input.begin() + (kBlocks / 2) * kBlock, input.end());
      return buffer_rms(settled) / buffer_rms(reference);
   }
}

void test_note_processor_registration()
{
   printf("\n=== Wave 3: Registration & Spawn Menu ===\n");

   auto& registry = WasmModuleAdapterRegistry::instance();
   const char* processors[] = { "chorder", "arpeggiator", "notefilter", "noterouter" };
   for (const char* type : processors)
   {
      const WasmModuleAdapter* adapter = registry.find(type);
      char name[96];
      std::snprintf(name, sizeof(name), "%s is a registered Note processor", type);
      TEST(name, adapter && adapter->audioRole() == WasmAudioRole::NoteProcessor &&
                 adapter->category() == ModuleCategory::NoteEffect &&
                 adapter->inputPorts().size() == 1 && adapter->inputPorts()[0].type == PortType::Note &&
                 !adapter->outputPorts().empty() && adapter->outputPorts()[0].type == PortType::Note);
   }
   const WasmModuleAdapter* router = registry.find("noterouter");
   TEST("Note router has four Note outputs", router && router->outputPorts().size() == 4);

   const WasmModuleAdapter* input = registry.find("input");
   TEST("Input is an audio source with no inputs and one Audio out",
        input && input->audioRole() == WasmAudioRole::AudioSource && input->inputPorts().empty() &&
        input->outputPorts().size() == 1 && input->outputPorts()[0].type == PortType::Audio);
   const WasmModuleAdapter* eq = registry.find("eq");
   TEST("EQ is an audio processor, Audio in and out",
        eq && eq->audioRole() == WasmAudioRole::AudioProcessor && eq->inputPorts().size() == 1 &&
        eq->outputPorts().size() == 1 && eq->category() == ModuleCategory::AudioEffect);
   TEST("Placeholder is never a registered type", registry.find("placeholder") == nullptr);

   // Spawn menu: every wave-3 type is spawnable, the Note chip lists the processors.
   auto spawnable = [](int category, const char* type)
   {
      for (const auto& info : spawnMenuTypes(category, ""))
      {
         if (info.type == type)
            return true;
      }
      return false;
   };
   const int noteCategory = static_cast<int>(ModuleCategory::NoteEffect);
   TEST("Spawn menu has input", spawnable(-1, "input"));
   TEST("Spawn menu has eq", spawnable(-1, "eq"));
   bool allInNoteChip = true;
   for (const char* type : processors)
      allInNoteChip = allInNoteChip && spawnable(noteCategory, type);
   TEST("Note chip lists chorder, arpeggiator, notefilter, noterouter", allInNoteChip);
   TEST("Note chip also lists existing note effects", spawnable(noteCategory, "pulsetonote"));
   TEST("Placeholder never appears in the spawn menu", spawnMenuTypes(-1, "placeholder").empty());

   std::set<int> chipCategories;
   bool chipsFit = true;
   bool hasNoteChip = false;
   for (int i = 0; i < kSpawnMenuChipCount; ++i)
   {
      chipCategories.insert(spawnMenuChip(i).category);
      hasNoteChip = hasNoteChip || (spawnMenuChip(i).category == noteCategory && std::string(spawnMenuChip(i).label) == "Note");
      chipsFit = chipsFit && 6.0f + (i + 1) * kSpawnMenuChipPitch <= 280.0f;
   }
   TEST("Spawn chips have distinct categories and a Note chip",
        static_cast<int>(chipCategories.size()) == kSpawnMenuChipCount && hasNoteChip);
   TEST("Spawn chips fit the 280px menu", chipsFit);
}

void test_note_processors_dsp()
{
   printf("\n=== Wave 3: Note Processors ===\n");

   // --- Chorder ---
   {
      Processor<ChorderModuleAdapter, ChorderParams> major({ { "chord", 0.0f } });
      auto on = major.run({ ev(60, 0.8f, true) });
      TEST("Chorder: major chord from one note",
           pitchesOf(on, true) == std::vector<int>({ 60, 64, 67 }) && pitchesOf(on, false).empty());
      auto off = major.run({ ev(60, 0.0f, false) });
      TEST("Chorder: note-off releases every chord tone",
           pitchesOf(off, false) == std::vector<int>({ 60, 64, 67 }) && pitchesOf(off, true).empty());
   }
   {
      Processor<ChorderModuleAdapter, ChorderParams> level({ { "chord", 0.0f }, { "level", 0.5f } });
      auto on = level.run({ ev(60, 0.8f, true) });
      TEST("Chorder: root keeps the played velocity, added tones scale by level",
           on.size() == 3 && std::fabs(on[0].velocity - 0.8f) < 1e-5f && std::fabs(on[1].velocity - 0.4f) < 1e-5f &&
           std::fabs(on[2].velocity - 0.4f) < 1e-5f);
   }
   {
      Processor<ChorderModuleAdapter, ChorderParams> overlap({ { "chord", 0.0f } });
      overlap.run({ ev(60, 0.8f, true) }); // 60 64 67
      auto second = overlap.run({ ev(64, 0.8f, true) }); // 64 68 71, 64 is already sounding
      TEST("Chorder: a shared tone is not retriggered", pitchesOf(second, true) == std::vector<int>({ 68, 71 }));
      auto releaseFirst = overlap.run({ ev(60, 0.0f, false) });
      TEST("Chorder: a shared tone stays on until its last key goes",
           pitchesOf(releaseFirst, false) == std::vector<int>({ 60, 67 }));
      auto releaseSecond = overlap.run({ ev(64, 0.0f, false) });
      TEST("Chorder: last key releases the rest", pitchesOf(releaseSecond, false) == std::vector<int>({ 64, 68, 71 }));
   }
   {
      Processor<ChorderModuleAdapter, ChorderParams> retrigger({ { "chord", 0.0f } });
      retrigger.run({ ev(60, 0.8f, true) });
      auto again = retrigger.run({ ev(60, 0.8f, true) });
      TEST("Chorder: a repeated note-on releases then replays the chord",
           pitchesOf(again, false).size() == 3 && pitchesOf(again, true).size() == 3);
   }
   {
      Processor<ChorderModuleAdapter, ChorderParams> inverted({ { "chord", 0.0f }, { "inversion", 1.0f } });
      auto on = inverted.run({ ev(60, 0.8f, true) });
      std::vector<int> pitches = pitchesOf(on, true);
      std::sort(pitches.begin(), pitches.end());
      TEST("Chorder: first inversion lifts the root an octave", pitches == std::vector<int>({ 64, 67, 72 }));
   }
   {
      Processor<ChorderModuleAdapter, ChorderParams> custom(
      { { "chord", static_cast<float>(kChorderCustomPreset) }, { "tone2", 3.0f }, { "tone3", 0.0f }, { "tone4", 10.0f } });
      auto on = custom.run({ ev(60, 0.8f, true) });
      TEST("Chorder: custom intervals skip zeroed tones", pitchesOf(on, true) == std::vector<int>({ 60, 63, 70 }));
   }
   {
      Processor<ChorderModuleAdapter, ChorderParams> top({ { "chord", 0.0f } });
      auto on = top.run({ ev(126, 0.8f, true) });
      TEST("Chorder: tones past MIDI 127 are dropped", pitchesOf(on, true) == std::vector<int>({ 126 }));
      auto off = top.run({ ev(126, 0.0f, false) });
      TEST("Chorder: dropped tones leave no stuck note", pitchesOf(off, false) == std::vector<int>({ 126 }));
   }
   {
      Processor<ChorderModuleAdapter, ChorderParams> tiny({ { "chord", 0.0f } });
      auto on = tiny.run({ ev(60, 0.8f, true) }, 0.0, 0.05, 2);
      TEST("Chorder: output is bounded by the caller's buffer", on.size() == 2);
   }
   {
      Processor<ChorderModuleAdapter, ChorderParams> channels({ { "chord", 0.0f } });
      auto on = channels.run({ ev(60, 0.8f, true, 5) });
      bool keepsChannel = on.size() == 3;
      for (const auto& e : on)
         keepsChannel = keepsChannel && e.channel == 5;
      TEST("Chorder: tones keep the input channel", keepsChannel);
   }

   // --- Arpeggiator (rate 2 = 1/16 = 0.25 beat steps; default gate 60%) ---
   auto arpOns = [](Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams>& arp, const std::vector<WasmNoteEvent>& held,
                    double beats, double blockBeats)
   {
      std::vector<int> ons;
      bool first = true;
      for (double b = 0.0; b < beats - 1e-9; b += blockBeats)
      {
         auto out = arp.run(first ? held : std::vector<WasmNoteEvent>{}, b, b + blockBeats);
         first = false;
         for (const auto& e : out)
         {
            if (e.isNoteOn)
               ons.push_back(e.pitch);
         }
      }
      return ons;
   };
   const std::vector<WasmNoteEvent> chordHeld = { ev(60, 0.9f, true), ev(64, 0.9f, true), ev(67, 0.9f, true) };
   {
      Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams> up({ { "mode", 0.0f } });
      TEST("Arp: up walks the held notes, one per step",
           arpOns(up, chordHeld, 1.25, 1.25) == std::vector<int>({ 60, 64, 67, 60, 64 }));
   }
   {
      Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams> blocks({ { "mode", 0.0f } });
      TEST("Arp: the pattern is the same across small blocks",
           arpOns(blocks, chordHeld, 1.25, 0.025) == std::vector<int>({ 60, 64, 67, 60, 64 }));
   }
   {
      Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams> down({ { "mode", 1.0f } });
      TEST("Arp: down", arpOns(down, chordHeld, 1.0, 1.0) == std::vector<int>({ 67, 64, 60, 67 }));
      Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams> upDown({ { "mode", 2.0f } });
      TEST("Arp: up/down turns around without repeating the ends",
           arpOns(upDown, chordHeld, 1.25, 1.25) == std::vector<int>({ 60, 64, 67, 64, 60 }));
      Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams> twoOctaves({ { "octaves", 2.0f } });
      TEST("Arp: octave range extends the pattern",
           arpOns(twoOctaves, chordHeld, 1.75, 1.75) == std::vector<int>({ 60, 64, 67, 72, 76, 79, 60 }));
      Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams> asPlayed({ { "mode", 4.0f } });
      TEST("Arp: as played follows press order",
           arpOns(asPlayed, { ev(67, 0.9f, true), ev(60, 0.9f, true), ev(64, 0.9f, true) }, 1.0, 1.0) ==
           std::vector<int>({ 67, 60, 64, 67 }));
   }
   {
      Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams> random({ { "mode", 3.0f } });
      const auto ons = arpOns(random, chordHeld, 4.0, 4.0);
      std::set<int> distinct(ons.begin(), ons.end());
      bool inChord = !ons.empty();
      for (int pitch : ons)
         inChord = inChord && (pitch == 60 || pitch == 64 || pitch == 67);
      TEST("Arp: random stays inside the held notes and varies", inChord && ons.size() == 16 && distinct.size() > 1);
   }
   {
      Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams> gated({ { "gate", 0.5f } });
      auto first = gated.run({ ev(60, 0.9f, true) }, 0.0, 0.1);
      TEST("Arp: the first step fires on the beat", first.size() == 1 && first[0].isNoteOn && first[0].pitch == 60);
      auto before = gated.run({}, 0.1, 0.12);
      auto after = gated.run({}, 0.12, 0.13);
      TEST("Arp: gate closes at its fraction of the step (0.125 beat)",
           before.empty() && after.size() == 1 && !after[0].isNoteOn && after[0].pitch == 60);
   }
   {
      Processor<ArpeggiatorModuleAdapter, ArpeggiatorParams> release({ { "gate", 1.0f } });
      release.run({ ev(60, 0.9f, true) }, 0.0, 0.05);
      auto out = release.run({ ev(60, 0.0f, false) }, 0.05, 0.1);
      TEST("Arp: releasing the last key stops the sounding note at once",
           out.size() == 1 && !out[0].isNoteOn && out[0].pitch == 60);
      TEST("Arp: nothing held plays nothing", release.run({}, 0.25, 1.5).empty());
   }

   // --- Note filter ---
   {
      Processor<NoteFilterModuleAdapter, NoteFilterParams> filter({ { "low", 48.0f }, { "high", 72.0f } });
      auto out = filter.run({ ev(40, 0.8f, true), ev(60, 0.8f, true), ev(80, 0.8f, true), ev(40, 0.0f, false),
                              ev(80, 0.0f, false) });
      TEST("Filter: only notes inside the range pass",
           pitchesOf(out, true) == std::vector<int>({ 60 }) && pitchesOf(out, false).empty());
      auto off = filter.run({ ev(60, 0.0f, false) });
      TEST("Filter: a passed note's off passes too", pitchesOf(off, false) == std::vector<int>({ 60 }));
   }
   {
      Processor<NoteFilterModuleAdapter, NoteFilterParams> filter({ { "low", 48.0f }, { "high", 72.0f } });
      filter.run({ ev(60, 0.8f, true) });
      filter.params.high = 50; // range edited while the key is held
      auto off = filter.run({ ev(60, 0.0f, false) });
      TEST("Filter: a range edit can't leave a note hanging", pitchesOf(off, false) == std::vector<int>({ 60 }));
   }
   {
      Processor<NoteFilterModuleAdapter, NoteFilterParams> outside(
      { { "low", 48.0f }, { "high", 72.0f }, { "mode", 1.0f } });
      auto out = outside.run({ ev(40, 0.8f, true), ev(60, 0.8f, true), ev(80, 0.8f, true) });
      TEST("Filter: outside mode inverts the range", pitchesOf(out, true) == std::vector<int>({ 40, 80 }));
   }

   // --- Note router ---
   {
      Processor<NoteRouterModuleAdapter, NoteRouterParams> robin({ { "outputs", 3.0f }, { "mode", 0.0f } });
      auto on = robin.run({ ev(60, 0.8f, true), ev(61, 0.8f, true), ev(62, 0.8f, true), ev(63, 0.8f, true) });
      std::vector<int> ports;
      for (const auto& e : on)
         ports.push_back(e.outputPort);
      TEST("Router: round robin deals notes to successive outputs", ports == std::vector<int>({ 0, 1, 2, 0 }));
      auto off = robin.run({ ev(61, 0.0f, false), ev(60, 0.0f, false) });
      TEST("Router: note-offs follow their note-on's output",
           off.size() == 2 && off[0].outputPort == 1 && off[1].outputPort == 0);
      auto stray = robin.run({ ev(100, 0.0f, false) });
      TEST("Router: an off for a note never routed is dropped", stray.empty());
   }
   {
      Processor<NoteRouterModuleAdapter, NoteRouterParams> split(
      { { "outputs", 3.0f }, { "mode", 1.0f }, { "split", 60.0f }, { "zone", 12.0f } });
      auto on = split.run({ ev(48, 0.8f, true), ev(60, 0.8f, true), ev(71, 0.8f, true), ev(72, 0.8f, true),
                            ev(100, 0.8f, true) });
      std::vector<int> ports;
      for (const auto& e : on)
         ports.push_back(e.outputPort);
      TEST("Router: pitch split sends zones to outputs, clamped to the last",
           ports == std::vector<int>({ 0, 1, 1, 2, 2 }));
      TEST("Router: splitOutput agrees", NoteRouterModuleAdapter::splitOutput(split.params, 59) == 0 &&
                                         NoteRouterModuleAdapter::splitOutput(split.params, 72) == 2);
   }
   {
      Processor<NoteRouterModuleAdapter, NoteRouterParams> retrigger({ { "outputs", 2.0f }, { "mode", 0.0f } });
      retrigger.run({ ev(60, 0.8f, true) });
      auto again = retrigger.run({ ev(60, 0.8f, true) });
      TEST("Router: a retrigger ends the old note on its old output first",
           again.size() == 2 && !again[0].isNoteOn && again[0].outputPort == 0 && again[1].isNoteOn &&
           again[1].outputPort == 1);
   }
}

void test_note_processor_graph()
{
   printf("\n=== Wave 3: Note Processor Graph ===\n");

   // Plan order is upstream-first whatever order the nodes were added in.
   {
      Rig rig;
      appendAudioGraphNode(rig.graph, 4, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(rig.graph, 3, "chorder", true, { { "chord", 0.0f } });
      appendAudioGraphNode(rig.graph, 2, "notefilter", true, { { "low", 0.0f }, { "high", 127.0f } });
      appendAudioGraphNode(rig.graph, 1, "stepsequencer", true,
                           { { "pattern", 1.0f }, { "pitch", 60.0f }, { "gate", 0.5f }, { "steps", 16.0f } });
      appendAudioGraphNode(rig.graph, 5, "output", true, {});
      rig.graph.connections.push_back(noteCable(1, 0, 2));
      rig.graph.connections.push_back(noteCable(2, 0, 3));
      rig.graph.connections.push_back(noteCable(3, 0, 4));
      rig.graph.connections.push_back(audioCable(4, 5));
      rig.prepare();

      const auto& plan = rig.graph.processPlan;
      TEST("Chain plan is valid", plan.valid && !plan.hasCycle);
      TEST("Chain plan orders sequencer, filter, chorder",
           planSlotOf(rig.graph, 1) == 0 && planSlotOf(rig.graph, 2) == 1 && planSlotOf(rig.graph, 3) == 2);
      const int chorderSlot = planSlotOf(rig.graph, 3);
      if (chorderSlot >= 0)
      {
         const auto& chorderPlan = plan.noteSourcePlans[static_cast<size_t>(chorderSlot)];
         TEST("Chorder plan reads exactly the earlier filter slot",
              chorderPlan.isProcessor && chorderPlan.hasNoteCable && chorderPlan.noteSourceScratchSlots.size() == 1 &&
              chorderPlan.noteSourceScratchSlots.data[0] < chorderSlot);
         TEST("Chorder (a processor with a cable) leaves the performance alone", !plan.performanceNotesProcessed);
         TEST("Chorder output reports to MIDI out", chorderPlan.reportsToMidiOut);
      }
      TEST("Emitters that only feed processors don't report to MIDI out",
           !plan.noteSourcePlans[0].reportsToMidiOut && !plan.noteSourcePlans[1].reportsToMidiOut);

      // sequencer -> filter -> chorder -> oscillator: the oscillator plays and MIDI out has the chord only.
      const float loudest = rig.run(60);
      TEST("Sequencer -> filter -> chorder -> oscillator is audible", loudest > 0.01f);
      std::vector<int> ons = rig.midiPitches(true);
      std::sort(ons.begin(), ons.end());
      TEST("MIDI out carries the chord, not the raw sequencer note", ons == std::vector<int>({ 60, 64, 67 }));
      std::vector<int> offs = rig.midiPitches(false);
      std::sort(offs.begin(), offs.end());
      TEST("MIDI out releases the whole chord", offs == std::vector<int>({ 60, 64, 67 }));
   }

   // Without processors the sequencer reports itself; with capture off nothing queues.
   {
      Rig plain;
      appendAudioGraphNode(plain.graph, 1, "stepsequencer", true,
                           { { "pattern", 1.0f }, { "pitch", 60.0f }, { "gate", 0.5f }, { "steps", 16.0f } });
      appendAudioGraphNode(plain.graph, 2, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(plain.graph, 3, "output", true, {});
      plain.graph.connections.push_back(noteCable(1, 0, 2));
      plain.graph.connections.push_back(audioCable(2, 3));
      plain.prepare();
      plain.run(30);
      TEST("Sequencer note reaches MIDI out with its off",
           plain.midiPitches(true) == std::vector<int>({ 60 }) && plain.midiPitches(false) == std::vector<int>({ 60 }));

      Rig quiet;
      quiet.graph = plain.graph;
      quiet.prepare(false);
      quiet.run(30);
      TEST("Capture off queues nothing", quiet.midi.empty());
   }

   // Stopping the transport ends notes the host would otherwise leave hanging.
   {
      Rig held;
      appendAudioGraphNode(held.graph, 1, "stepsequencer", true,
                           { { "pattern", 1.0f }, { "pitch", 64.0f }, { "gate", 1.0f }, { "steps", 16.0f } });
      appendAudioGraphNode(held.graph, 2, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(held.graph, 3, "output", true, {});
      held.graph.connections.push_back(noteCable(1, 0, 2));
      held.graph.connections.push_back(audioCable(2, 3));
      held.prepare();
      held.run(2);
      const size_t offsBefore = held.midiPitches(false).size();
      held.graph.transportPlaying = false;
      held.run(2);
      TEST("Transport stop flushes the held MIDI note",
           held.midiPitches(true) == std::vector<int>({ 64 }) && offsBefore == 0 &&
           held.midiPitches(false) == std::vector<int>({ 64 }));
   }

   // Performance path: a processor with no Note cable listens to the keyboard / MIDI in.
   {
      Rig perf;
      appendAudioGraphNode(perf.graph, 1, "chorder", true, { { "chord", 0.0f } });
      appendAudioGraphNode(perf.graph, 2, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(perf.graph, 3, "output", true, {});
      perf.graph.connections.push_back(noteCable(1, 0, 2));
      perf.graph.connections.push_back(audioCable(2, 3));
      perf.prepare();
      TEST("An uncabled processor takes over the performance", perf.graph.processPlan.performanceNotesProcessed);
      perf.queue(60, 0.9f, true);
      const float loudest = perf.run(6);
      TEST("Keyboard note plays a chord through chorder -> oscillator", loudest > 0.01f);
      std::vector<int> ons = perf.midiPitches(true);
      std::sort(ons.begin(), ons.end());
      TEST("MIDI out sees the chord for a performed note", ons == std::vector<int>({ 60, 64, 67 }));
      perf.queue(60, 0.0f, false);
      perf.run(2);
      TEST("Releasing the key releases the chord", perf.midiPitches(false).size() == 3);
   }
   {
      Rig direct;
      appendAudioGraphNode(direct.graph, 1, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(direct.graph, 2, "output", true, {});
      direct.graph.connections.push_back(audioCable(1, 2));
      direct.prepare();
      TEST("No processors: the performance is not processed", !direct.graph.processPlan.performanceNotesProcessed);
      direct.queue(60, 0.9f, true);
      direct.run(4);
      TEST("Performance notes are not reported by the engine (the host echoes them)", direct.midi.empty());
   }

   // Arpeggiator on the performance + transport: a pattern with no further input.
   {
      Rig arp;
      appendAudioGraphNode(arp.graph, 1, "arpeggiator", true,
                           { { "rate", 2.0f }, { "mode", 0.0f }, { "octaves", 1.0f }, { "gate", 0.6f } });
      appendAudioGraphNode(arp.graph, 2, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(arp.graph, 3, "output", true, {});
      arp.graph.connections.push_back(noteCable(1, 0, 2));
      arp.graph.connections.push_back(audioCable(2, 3));
      arp.prepare();
      arp.queue(60, 0.9f, true);
      arp.queue(64, 0.9f, true);
      arp.queue(67, 0.9f, true);
      const float loudest = arp.run(90); // ~1 s: 8 steps at 120 BPM
      const std::vector<int> ons = arp.midiPitches(true);
      TEST("Arp plays a repeating pattern from held keys and transport", loudest > 0.01f && ons.size() >= 6);
      bool cycles = ons.size() >= 6;
      const int expected[3] = { 60, 64, 67 };
      for (size_t i = 0; cycles && i < ons.size(); ++i)
         cycles = ons[i] == expected[i % 3];
      TEST("Arp pattern cycles up through the held notes", cycles);
      TEST("Each arp note is released", arp.midiPitches(false).size() + 1 >= ons.size());
   }

   // Router: cable ordinals filter which output feeds which consumer.
   {
      Rig routed;
      appendAudioGraphNode(routed.graph, 1, "noterouter", true,
                           { { "outputs", 2.0f }, { "mode", 1.0f }, { "split", 60.0f }, { "zone", 12.0f } });
      appendAudioGraphNode(routed.graph, 2, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(routed.graph, 3, "output", true, {});
      routed.graph.connections.push_back(noteCable(1, 1, 2)); // router output 2 only
      routed.graph.connections.push_back(audioCable(2, 3));
      routed.prepare();
      routed.queue(48, 0.9f, true); // below the split: output 1, which nothing is cabled to
      const float low = routed.run(8);
      TEST("Router: a note on the other output doesn't reach the consumer", low < 1e-4f);
      routed.queue(48, 0.0f, false);
      routed.queue(72, 0.9f, true); // output 2
      const float high = routed.run(8);
      TEST("Router: a note on the cabled output plays", high > 0.01f);
   }
   {
      Rig fan;
      appendAudioGraphNode(fan.graph, 1, "noterouter", true, { { "outputs", 2.0f } });
      appendAudioGraphNode(fan.graph, 2, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(fan.graph, 3, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(fan.graph, 4, "output", true, {});
      fan.graph.connections.push_back(noteCable(1, 0, 2));
      fan.graph.connections.push_back(noteCable(1, 1, 3));
      fan.graph.connections.push_back(audioCable(2, 4));
      fan.graph.connections.push_back(audioCable(3, 4));
      fan.prepare();
      int filtersFound = 0;
      for (const auto& step : fan.graph.processPlan.steps)
      {
         if (step.moduleId == 2 && step.noteSourcePortFilters.size() == 1 && step.noteSourcePortFilters.data[0] == 0)
            ++filtersFound;
         if (step.moduleId == 3 && step.noteSourcePortFilters.size() == 1 && step.noteSourcePortFilters.data[0] == 1)
            ++filtersFound;
      }
      TEST("Router outputs map to per-consumer port filters", filtersFound == 2);
   }

   // A processor chained onto a processor: nothing reports twice.
   {
      Rig chain;
      appendAudioGraphNode(chain.graph, 1, "stepsequencer", true,
                           { { "pattern", 1.0f }, { "pitch", 60.0f }, { "gate", 0.5f }, { "steps", 16.0f } });
      appendAudioGraphNode(chain.graph, 2, "chorder", true, { { "chord", 9.0f } }); // power: 0, 7, 12
      appendAudioGraphNode(chain.graph, 3, "notefilter", true, { { "low", 0.0f }, { "high", 70.0f } });
      appendAudioGraphNode(chain.graph, 4, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(chain.graph, 5, "output", true, {});
      chain.graph.connections.push_back(noteCable(1, 0, 2));
      chain.graph.connections.push_back(noteCable(2, 0, 3));
      chain.graph.connections.push_back(noteCable(3, 0, 4));
      chain.graph.connections.push_back(audioCable(4, 5));
      chain.prepare();
      chain.run(40);
      std::vector<int> ons = chain.midiPitches(true);
      std::sort(ons.begin(), ons.end());
      TEST("Chorder -> filter: MIDI out gets only what the filter lets through", ons == std::vector<int>({ 60, 67 }));
   }

   // A patch swap resets the live engine in place: it stays prepared (and audible), keeps capture on,
   // and releases whatever the old patch was still holding on the MIDI output.
   {
      Rig swap;
      appendAudioGraphNode(swap.graph, 1, "stepsequencer", true,
                           { { "pattern", 1.0f }, { "pitch", 60.0f }, { "gate", 0.5f }, { "steps", 16.0f } });
      appendAudioGraphNode(swap.graph, 2, "oscillator", true, { { "volume", 1.0f } });
      appendAudioGraphNode(swap.graph, 3, "output", true, { { "level", 0.8f } });
      swap.graph.connections.push_back(noteCable(1, 0, 2));
      swap.graph.connections.push_back(audioCable(2, 3));
      swap.prepare();
      swap.run(2);
      TEST("Swap setup: a note is sounding on the MIDI output", swap.midiPitches(true).size() == 1 && swap.midiPitches(false).empty());

      swap.engine.resetClock();
      swap.engine.prepareRuntimeStates(swap.graph);
      swap.run(1);
      TEST("Reset queues the note-off for the held note", swap.midiPitches(false) == std::vector<int>({ 60 }));
      TEST("Reset restarts the transport clock", swap.engine.beatPosition() < 0.1);

      TEST("The reset engine is still audible without growing state in the block", swap.run(60) > 0.01f);
      TEST("The reset engine still captures MIDI out", swap.midiPitches(true).size() >= 2);
   }
}

void test_input_module()
{
   printf("\n=== Wave 3: Input Module ===\n");

   auto& bus = InputAudioBus::instance();
   auto drain = [&bus]()
   {
      float scratch[4096];
      bus.consume(scratch, 4096);
      bus.takePeak();
   };
   drain();

   const std::vector<float> tone = sine(512, 440.0f, 0.5f);
   auto renderInput = [&](const WasmControlMap& controls)
   {
      Rig rig;
      appendAudioGraphNode(rig.graph, 1, "input", true, controls);
      appendAudioGraphNode(rig.graph, 2, "output", true, {});
      rig.graph.connections.push_back(audioCable(1, 2));
      rig.prepare(false);
      float loudest = 0.0f;
      for (int b = 0; b < 6; ++b)
      {
         bus.push(tone.data(), 512);
         loudest = std::max(loudest, rig.run(1));
      }
      return loudest;
   };

   const float full = renderInput({ { "gain", 1.0f }, { "mute", 0.0f } });
   TEST("Input is audible when the mic bus has signal", full > 0.05f);
   const float half = renderInput({ { "gain", 0.5f }, { "mute", 0.0f } });
   TEST("Input gain scales the signal", half > 0.0f && std::fabs(half / full - 0.5f) < 0.05f);
   TEST("Input mute silences it", renderInput({ { "gain", 1.0f }, { "mute", 1.0f } }) < 1e-6f);
   drain();
   {
      Rig empty;
      appendAudioGraphNode(empty.graph, 1, "input", true, {});
      appendAudioGraphNode(empty.graph, 2, "output", true, {});
      empty.graph.connections.push_back(audioCable(1, 2));
      empty.prepare(false);
      TEST("Nothing pushed means nothing out", empty.run(3) < 1e-6f);
   }

   bus.push(tone.data(), 512);
   {
      Rig meter;
      appendAudioGraphNode(meter.graph, 1, "input", true, {});
      appendAudioGraphNode(meter.graph, 2, "output", true, {});
      meter.graph.connections.push_back(audioCable(1, 2));
      meter.prepare(false);
      meter.run(1);
   }
   const float peak = bus.takePeak();
   TEST("Input bus reports the level it consumed", peak > 0.45f && peak <= 0.5f + 1e-4f);
   TEST("Peak resets on read", bus.takePeak() == 0.0f);
   bus.setCaptureActive(true);
   TEST("Capture flag is readable", bus.captureActive());
   bus.setCaptureActive(false);
   TEST("Capture flag clears", !bus.captureActive());

   // Input -> looper: record a bar from the mic through the cable, play it back after the mic stops.
   {
      Rig looped;
      looped.graph.transportBPM = 240.0f; // one 4/4 bar = 1 s
      appendAudioGraphNode(looped.graph, 1, "input", true, {});
      appendAudioGraphNode(looped.graph, 2, "looper", true, { { "bars", 1.0f }, { "mix", 1.0f }, { "command", 0.0f } });
      appendAudioGraphNode(looped.graph, 3, "output", true, {});
      looped.graph.connections.push_back(audioCable(1, 2));
      looped.graph.connections.push_back(audioCable(2, 3));
      looped.prepare(false);
      auto* looperParams = static_cast<LooperParams*>(looped.graph.paramsFor(looped.graph.nodes[1]));
      TEST("Looper node has params", looperParams != nullptr);
      if (looperParams)
      {
         looperParams->command = 1; // record at the next bar
         for (int b = 0; b < 100; ++b) // 1 s to the bar line, then ~1 s recording
         {
            bus.push(tone.data(), 512);
            looped.run(1);
         }
         for (int b = 0; b < 100; ++b)
         {
            bus.push(tone.data(), 512);
            looped.run(1);
         }
         LooperArena* arena = LooperArenaPool::instance().find(2);
         TEST("Looper recorded the cabled input", arena && arena->hasLoop && arena->loopLength > 0);
         double energy = 0.0;
         if (arena && arena->hasLoop)
         {
            for (int i = 0; i < arena->loopLength; ++i)
               energy += static_cast<double>(arena->plane(0)[i]) * arena->plane(0)[i];
            energy = std::sqrt(energy / arena->loopLength);
         }
         TEST("Recorded loop holds the input signal", energy > 0.3 && energy < 0.4);
         drain();
         const float playback = looped.run(10); // mic bus empty: only the loop can be heard
         TEST("Loop plays back with the mic silent", playback > 0.05f);
      }
      LooperArenaPool::instance().release(2);
   }
   drain();
}

void test_eq_module()
{
   printf("\n=== Wave 3: EQ ===\n");

   TEST("EQ defaults are flat", std::fabs(eqGainAt({}, 1000.0f) - 1.0f) < 1e-4f);
   {
      EqModuleAdapter adapter;
      EqParams params{};
      adapter.fillParams({}, &params);
      std::vector<uint8_t> runtime(adapter.runtimeStateSize());
      adapter.initRuntimeState(runtime.data());
      std::vector<float> l = sine(512, 700.0f, 0.5f);
      const std::vector<float> original = l;
      std::vector<float> r = l;
      float* channels[2] = { l.data(), r.data() };
      WasmAudioProcessContext ctx;
      ctx.numSamples = 512;
      ctx.numChannels = 2;
      adapter.processAudio(runtime.data(), &params, channels, ctx);
      float worst = 0.0f;
      for (size_t i = 0; i < l.size(); ++i)
         worst = std::max(worst, std::fabs(l[i] - original[i]));
      TEST("Flat EQ is a bit-exact bypass", worst == 0.0f);
      adapter.destroyRuntimeState(runtime.data());
   }

   const float peakBoost = eqGainAt({ { "midGain", 12.0f }, { "midFreq", 1000.0f }, { "midQ", 1.0f } }, 1000.0f);
   TEST("Peak band boosts its centre by its gain (+12 dB = 3.98x)", peakBoost > 3.8f && peakBoost < 4.15f);
   const float peakCut = eqGainAt({ { "midGain", -12.0f }, { "midFreq", 1000.0f }, { "midQ", 1.0f } }, 1000.0f);
   TEST("Peak band cuts its centre (-12 dB = 0.25x)", peakCut > 0.23f && peakCut < 0.27f);
   const float peakFar = eqGainAt({ { "midGain", 12.0f }, { "midFreq", 1000.0f }, { "midQ", 4.0f } }, 100.0f);
   TEST("A narrow peak leaves distant frequencies alone", std::fabs(peakFar - 1.0f) < 0.05f);

   const float lowShelf = eqGainAt({ { "lowGain", 12.0f }, { "lowFreq", 200.0f } }, 40.0f);
   TEST("Low shelf lifts the bass", lowShelf > 3.4f && lowShelf < 4.3f);
   TEST("Low shelf leaves the treble alone",
        std::fabs(eqGainAt({ { "lowGain", 12.0f }, { "lowFreq", 200.0f } }, 5000.0f) - 1.0f) < 0.03f);
   const float highShelf = eqGainAt({ { "highGain", -12.0f }, { "highFreq", 4000.0f } }, 14000.0f);
   TEST("High shelf cuts the top", highShelf > 0.2f && highShelf < 0.32f);
   TEST("High shelf leaves the bass alone",
        std::fabs(eqGainAt({ { "highGain", -12.0f }, { "highFreq", 4000.0f } }, 100.0f) - 1.0f) < 0.03f);
   const float trimmed = eqGainAt({ { "output", -6.0f } }, 1000.0f);
   TEST("Output trim is in dB (-6 dB = 0.5x)", trimmed > 0.49f && trimmed < 0.51f);

   {
      EqModuleAdapter adapter;
      EqParams params{};
      adapter.fillParams({ { "midGain", 12.0f } }, &params);
      std::vector<uint8_t> runtime(adapter.runtimeStateSize());
      adapter.initRuntimeState(runtime.data());
      std::vector<float> l(512, 0.0f); // silent left, tone on the right
      std::vector<float> r = sine(512, 1000.0f, 0.5f);
      float* channels[2] = { l.data(), r.data() };
      WasmAudioProcessContext ctx;
      ctx.numSamples = 512;
      ctx.numChannels = 2;
      ctx.inputChannels = 2;
      for (int b = 0; b < 4; ++b)
         adapter.processAudio(runtime.data(), &params, channels, ctx);
      TEST("EQ channels are independent (silent left stays silent)", buffer_rms(l) == 0.0f && buffer_rms(r) > 0.0f);
      adapter.destroyRuntimeState(runtime.data());
   }

   // In a graph: automating a band re-voices a harmonically rich oscillator.
   {
      Rig rig;
      appendAudioGraphNode(rig.graph, 1, "oscillator", true, { { "frequency", 220.0f }, { "volume", 0.8f }, { "waveform", 1.0f } });
      appendAudioGraphNode(rig.graph, 2, "eq", true, {});
      appendAudioGraphNode(rig.graph, 3, "output", true, {});
      rig.graph.connections.push_back(audioCable(1, 2));
      rig.graph.connections.push_back(audioCable(2, 3));
      rig.prepare(false);
      const float flat = rig.run(20);
      auto* params = static_cast<EqParams*>(rig.graph.paramsFor(rig.graph.nodes[1]));
      TEST("EQ node params live in the arena", params != nullptr);
      if (params)
      {
         params->band[3].gainDb = -18.0f; // high shelf just above the fundamental
         params->band[3].freq = 250.0f;
         const float dark = rig.run(20);
         TEST("Cutting the highs darkens the voice (lower RMS)", flat > 0.01f && dark < flat * 0.85f);
         params->band[3].gainDb = 0.0f;
         params->band[0].gainDb = 12.0f;
         params->band[0].freq = 400.0f;
         const float warm = rig.run(20);
         TEST("Lifting the lows brightens the level", warm > flat * 1.2f);
      }
   }

   // Control edits go through the canvas commands: automation-ready and undoable.
   {
      ModuleCanvas canvas;
      canvas.clearUndoHistory();
      const int eq = canvas.createModule("eq", 100, 100);
      TEST("Canvas spawns an EQ", eq > 0 && canvas.getModule(eq) != nullptr);
      float value = -1.0f;
      canvas.getModuleControlValue(eq, "midGain", value);
      TEST("EQ mid gain starts flat", value == 0.0f);
      TEST("EQ control edit is accepted", canvas.setModuleControlValue(eq, "midGain", 9.0f));
      canvas.getModuleControlValue(eq, "midGain", value);
      TEST("EQ control edit sticks", value == 9.0f);
      const auto graph = canvas.getAudioGraphSnapshot();
      bool published = false;
      for (const auto& node : graph->nodes)
      {
         if (node.id == eq)
         {
            const auto* eqParams = static_cast<const EqParams*>(graph->paramsFor(node));
            published = eqParams && eqParams->band[1].gainDb == 9.0f;
         }
      }
      TEST("Edit is published to the audio graph", published);
      TEST("EQ edit undoes", canvas.undo());
      canvas.getModuleControlValue(eq, "midGain", value);
      TEST("Undo restores the previous value", value == 0.0f);
      const int depth = canvas.undoDepth();
      TEST("Automation writes skip undo",
           canvas.setModuleControlValue(eq, "midGain", 4.0f, false) && canvas.undoDepth() == depth);
   }
}

void test_bundled_drum_kit()
{
   printf("\n=== Wave 3: Bundled Drum Kit ===\n");

   auto& store = SampleStore::instance();
   store.clear();
   store.setDeviceSampleRate(48000);

   const BundledKitSample* kit = bundledKitSamples();
   TEST("Kit has at least four pieces", kBundledKitSampleCount >= 4);
   size_t totalBytes = 0;
   bool allValid = true;
   bool distinctRoots = true;
   std::set<int> roots;
   for (int i = 0; i < kBundledKitSampleCount; ++i)
   {
      const std::string path = std::string(bundledKitDirectory()) + "/" + kit[i].name + ".wav";
      FILE* file = std::fopen(path.c_str(), "rb");
      std::vector<uint8_t> bytes;
      if (file)
      {
         uint8_t chunk[4096];
         size_t got;
         while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
            bytes.insert(bytes.end(), chunk, chunk + got);
         std::fclose(file);
      }
      totalBytes += bytes.size();
      allValid = allValid && bytes.size() > 2000 && sha256Hex(bytes.data(), bytes.size()) == kit[i].sha256;
      distinctRoots = distinctRoots && roots.insert(kit[i].rootPitch).second;
   }
   TEST("Every kit file is present and matches its table hash (rerun gen_drum_kit.py if not)", allValid);
   TEST("Kit pieces trigger from distinct notes", distinctRoots);
   TEST("Kit is a few hundred KB at most", totalBytes > 20000 && totalBytes < 500 * 1024);

   bool resolved = true;
   bool atDeviceRate = true;
   bool audible = true;
   for (int i = 0; i < kBundledKitSampleCount; ++i)
   {
      const SampleBuffer* sample = findSampleOrBundled(kit[i].sha256);
      resolved = resolved && sample != nullptr && sample->frameCount() > 1000;
      if (sample)
      {
         atDeviceRate = atDeviceRate && sample->sampleRate() == 48000;
         float peak = 0.0f;
         for (int f = 0; f < sample->frameCount(); ++f)
            peak = std::max(peak, std::fabs(sample->channelData(0)[f]));
         audible = audible && peak > 0.3f && peak <= 1.0f;
      }
   }
   TEST("Looking up a kit hash decodes the piece on demand", resolved);
   TEST("Kit is resampled to the device rate", atDeviceRate);
   TEST("Kit pieces are normalised, audible one-shots", audible);
   TEST("A second lookup returns the stored sample",
        findSampleOrBundled(kit[0].sha256) == store.findByHash(kit[0].sha256));
   TEST("An unknown hash stays unresolved", findSampleOrBundled("0000") == nullptr);

   // A sampler pointed at a kit hash plays from a note without the user loading anything.
   {
      Rig rig;
      appendAudioGraphNode(rig.graph, 1, "sampler", true, { { "root", 36.0f }, { "volume", 1.0f } },
                           { { "sampleHash", kit[0].sha256 } });
      appendAudioGraphNode(rig.graph, 2, "output", true, {});
      rig.graph.connections.push_back(audioCable(1, 2));
      rig.prepare(false);
      rig.queue(36, 1.0f, true);
      TEST("Sampler with a kit hash plays the kick", rig.run(20) > 0.05f);
   }

   // The bundled patch: drum machine -> four kit samplers -> mixer -> compressor -> output.
   {
      std::string json;
      FILE* file = std::fopen("/resource/userdata_original/savestate/wasm-drumkit.bsk", "rb");
      if (file)
      {
         char chunk[4096];
         size_t got;
         while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
            json.append(chunk, got);
         std::fclose(file);
      }
      ModuleCanvas::StateSnapshot snapshot;
      int viewMode = 0;
      std::string error;
      TEST("Drum-kit patch is bundled and parses", !json.empty() && deserializePatchState(json, snapshot, viewMode, error));
      int samplers = 0;
      bool hashesKnown = true;
      for (const auto& module : snapshot.modules)
      {
         if (module.type != "sampler")
            continue;
         ++samplers;
         auto it = module.extras.find("sampleHash");
         bool known = false;
         for (int i = 0; it != module.extras.end() && i < kBundledKitSampleCount; ++i)
            known = known || it->second == kit[i].sha256;
         hashesKnown = hashesKnown && known;
      }
      TEST("Drum-kit patch uses four kit samplers", samplers == 4 && hashesKnown);

      ModuleCanvas canvas;
      TEST("Drum-kit patch loads", canvas.applyStateSnapshot(snapshot) && canvas.placeholderCount() == 0);
      canvas.setTransportPlaying(true);
      AudioGraphEngine engine;
      engine.prepareForBlock(512, 128, 32);
      const auto graph = canvas.getAudioGraphSnapshot();
      engine.prepareRuntimeStates(*graph);
      TEST("Drum-kit plan is valid", graph->processPlan.valid);
      bool allSamplesBound = true;
      int samplerNodes = 0;
      for (const auto& step : graph->processPlan.steps)
      {
         if (step.moduleId <= 0)
            continue;
         for (const auto& node : graph->nodes)
         {
            if (node.id != step.moduleId || WasmModuleAdapterRegistry::instance().adapterAt(node.typeIndex)->typeId() != std::string("sampler"))
               continue;
            ++samplerNodes;
            const auto* params = static_cast<const SamplerParams*>(graph->paramsFor(node));
            allSamplesBound = allSamplesBound && params && params->sample != nullptr;
         }
      }
      TEST("Every sampler in the patch resolved its kit sample", samplerNodes == 4 && allSamplesBound);
      std::vector<float> left(512), right(512);
      float loudest = 0.0f;
      for (int b = 0; b < 180; ++b) // ~2 s: a full bar of the groove
      {
         float* outputs[2] = { left.data(), right.data() };
         gAudioCallbackActive.store(true, std::memory_order_release);
         engine.processBlock(*graph, outputs, 2, 512, 48000.0f);
         gAudioCallbackActive.store(false, std::memory_order_release);
         loudest = std::max(loudest, buffer_rms(left));
      }
      TEST("Drum-kit patch plays a beat", loudest > 0.02f);
   }

   store.clear();
   store.setDeviceSampleRate(44100);
}

void test_wave3_saved_state_fixture()
{
   printf("\n=== Wave 3: Saved-State Fixture ===\n");
   // Fixture: mic -> EQ -> output, and a note chain sequencer -> filter -> router, whose two
   // outputs feed a chorder and an arpeggiator that each play an oscillator.
   const char* fixture =
   "{\n"
   "  \"filetype\": \"bespokesynth\",\n"
   "  \"format\": \"bespokesynth-wasm-state\",\n"
   "  \"version\": 4,\n"
   "  \"transport\": { \"bpm\": 120, \"playing\": true },\n"
   "  \"modules\": [\n"
   "    { \"id\": 1, \"type\": \"transport\", \"position\": { \"x\": 0, \"y\": 0 }, \"controls\": { \"bpm\": 120 } },\n"
   "    { \"id\": 2, \"type\": \"input\", \"position\": { \"x\": 0, \"y\": 0 }, \"controls\": { \"gain\": 1.2, \"mute\": 0 } },\n"
   "    { \"id\": 3, \"type\": \"eq\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"lowFreq\": 120, \"lowGain\": 3, \"midFreq\": 1500, \"midGain\": -4, \"midQ\": 1.2,\n"
   "                      \"hiMidFreq\": 4000, \"hiMidGain\": 2, \"hiMidQ\": 0.8, \"highFreq\": 9000, \"highGain\": 1.5,\n"
   "                      \"output\": -1 } },\n"
   "    { \"id\": 4, \"type\": \"output\", \"position\": { \"x\": 0, \"y\": 0 }, \"controls\": {} },\n"
   "    { \"id\": 5, \"type\": \"stepsequencer\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"pattern\": 4369, \"pitch\": 60, \"gate\": 0.5, \"steps\": 16 } },\n"
   "    { \"id\": 6, \"type\": \"notefilter\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"low\": 36, \"high\": 96, \"mode\": 0 } },\n"
   "    { \"id\": 7, \"type\": \"noterouter\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"outputs\": 2, \"mode\": 0, \"split\": 60, \"zone\": 12 } },\n"
   "    { \"id\": 8, \"type\": \"chorder\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"chord\": 1, \"inversion\": 0, \"tone2\": 4, \"tone3\": 7, \"tone4\": 0, \"level\": 0.8 } },\n"
   "    { \"id\": 9, \"type\": \"arpeggiator\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"rate\": 1, \"mode\": 2, \"octaves\": 2, \"gate\": 0.5 } },\n"
   "    { \"id\": 10, \"type\": \"oscillator\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"frequency\": 220, \"volume\": 0.5, \"waveform\": 0 } },\n"
   "    { \"id\": 11, \"type\": \"oscillator\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"frequency\": 220, \"volume\": 0.5, \"waveform\": 1 } }\n"
   "  ],\n"
   "  \"connections\": [\n"
   "    { \"source\": { \"module\": 2, \"port\": 0 }, \"destination\": { \"module\": 3, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 3, \"port\": 0 }, \"destination\": { \"module\": 4, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 5, \"port\": 0 }, \"destination\": { \"module\": 6, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 6, \"port\": 0 }, \"destination\": { \"module\": 7, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 7, \"port\": 0 }, \"destination\": { \"module\": 8, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 7, \"port\": 1 }, \"destination\": { \"module\": 9, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 8, \"port\": 0 }, \"destination\": { \"module\": 10, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 9, \"port\": 0 }, \"destination\": { \"module\": 11, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 10, \"port\": 0 }, \"destination\": { \"module\": 4, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 11, \"port\": 0 }, \"destination\": { \"module\": 4, \"port\": 0 } }\n"
   "  ]\n"
   "}\n";

   ModuleCanvas::StateSnapshot loaded;
   int viewMode = 0;
   std::string error;
   TEST("Wave-3 fixture deserializes", deserializePatchState(fixture, loaded, viewMode, error));
   TEST("Wave-3 fixture keeps all modules and cables", loaded.modules.size() == 11 && loaded.connections.size() == 10);
   TEST("Wave 3 did not bump the patch schema", loaded.schemaVersion == kPatchSchemaVersion);

   const int baselineModules = ModuleCanvas().getModuleCount(); // the canvas always owns a transport and scale
   ModuleCanvas canvas;
   TEST("Wave-3 fixture applies to a canvas", canvas.applyStateSnapshot(loaded));
   TEST("Every wave-3 type is known (no placeholders)",
        canvas.placeholderCount() == 0 && canvas.getModuleCount() == baselineModules + 10);

   const auto graph = canvas.getAudioGraphSnapshot();
   TEST("Wave-3 graph compiles", graph && graph->processPlan.valid && !graph->processPlan.hasCycle);
   if (graph)
   {
      const int seq = planSlotOf(*graph, canvas_module_id(canvas, "stepsequencer"));
      const int filter = planSlotOf(*graph, canvas_module_id(canvas, "notefilter"));
      const int router = planSlotOf(*graph, canvas_module_id(canvas, "noterouter"));
      const int chorder = planSlotOf(*graph, canvas_module_id(canvas, "chorder"));
      const int arp = planSlotOf(*graph, canvas_module_id(canvas, "arpeggiator"));
      TEST("Note chain is ordered upstream-first",
           seq >= 0 && seq < filter && filter < router && router < chorder && router < arp);
      TEST("Leaf processors report to MIDI out, interior ones don't",
           chorder >= 0 && arp >= 0 && router >= 0 &&
           graph->processPlan.noteSourcePlans[static_cast<size_t>(chorder)].reportsToMidiOut &&
           graph->processPlan.noteSourcePlans[static_cast<size_t>(arp)].reportsToMidiOut &&
           !graph->processPlan.noteSourcePlans[static_cast<size_t>(router)].reportsToMidiOut);
   }

   const std::string reserialized = serializePatchState(canvas.createStateSnapshot(), 0);
   ModuleCanvas::StateSnapshot roundTrip;
   TEST("Wave-3 fixture re-serializes", deserializePatchState(reserialized, roundTrip, viewMode, error));

   // Compare by saved type (ids are renumbered on load, and saves list modules newest first), so each
   // fixture module must find a distinct reloaded module of its type holding every control it set.
   bool controlsKept = roundTrip.modules.size() >= loaded.modules.size(); // the canvas adds its scale module
   std::vector<bool> used(roundTrip.modules.size(), false);
   for (const auto& module : loaded.modules)
   {
      bool matched = false;
      for (size_t i = 0; !matched && i < roundTrip.modules.size(); ++i)
      {
         const auto& candidate = roundTrip.modules[i];
         if (used[i] || candidate.type != module.type)
            continue;
         bool same = true; // the canvas may add defaults, so only the fixture's own controls are checked
         for (const auto& [name, value] : module.controls)
         {
            const auto control = candidate.controls.find(name);
            same = same && control != candidate.controls.end() && std::fabs(control->second - value) < 1e-4f;
         }
         if (same)
         {
            used[i] = true;
            matched = true;
         }
      }
      if (!matched)
         printf("  no reloaded %s carries the fixture's controls\n", module.type.c_str());
      controlsKept = controlsKept && matched;
   }
   TEST("Wave-3 fixture controls round-trip", controlsKept);

   auto cablesByType = [](const ModuleCanvas::StateSnapshot& snapshot)
   {
      std::map<int, std::string> typeById;
      for (const auto& module : snapshot.modules)
         typeById[module.id] = module.type;
      std::vector<std::tuple<std::string, int, std::string, int>> cables;
      for (const auto& c : snapshot.connections)
         cables.emplace_back(typeById[c.sourceModuleId], c.sourcePortIndex, typeById[c.destModuleId], c.destPortIndex);
      std::sort(cables.begin(), cables.end());
      return cables;
   };
   TEST("Wave-3 fixture cable ports round-trip",
        loaded.connections.size() == 10 && cablesByType(loaded) == cablesByType(roundTrip));
   ModuleCanvas again;
   again.applyStateSnapshot(roundTrip);
   TEST("A second save of the wave-3 patch is identical",
        canonical_by_type(again.createStateSnapshot()) == canonical_by_type(canvas.createStateSnapshot()));

   // And it plays: the router deals the 1-in-4 notes to the chorder and arp.
   Rig rig;
   rig.graph = *graph;
   rig.prepare();
   const float loudest = rig.run(90);
   TEST("Wave-3 patch makes sound from its note chain", loudest > 0.005f);
   TEST("Wave-3 patch reports its processed notes to MIDI out", !rig.midiPitches(true).empty());
}
