// Runtime safety nets: the note ring, notes played while the transport is stopped, and the
// fan-in limit ModuleCanvas enforces for the process plan.
#include "test_common.h"

namespace
{
   AudioGraphEngine::NoteMessage note(int pitch, bool on, float velocity = 0.8f)
   {
      AudioGraphEngine::NoteMessage message;
      message.pitch = pitch;
      message.isNoteOn = on;
      message.velocity = on ? velocity : 0.0f;
      return message;
   }

   /** Pass-through note filter on the performance -> oscillator -> output; MIDI out sees what the graph plays. */
   struct PerformanceRig
   {
      AudioGraphSnapshot graph;
      AudioGraphEngine engine;
      std::vector<AudioGraphEngine::MidiOutNote> midi;
      std::vector<float> left = std::vector<float>(512);
      std::vector<float> right = std::vector<float>(512);

      PerformanceRig()
      {
         appendAudioGraphNode(graph, 1, "notefilter", true, { { "low", 0.0f }, { "high", 127.0f } });
         appendAudioGraphNode(graph, 2, "oscillator", true, { { "volume", 1.0f } });
         appendAudioGraphNode(graph, 3, "output", true, {});
         graph.connections.push_back({ 1, 0, 2, 0, PortType::Note, PortType::Note });
         graph.connections.push_back({ 2, 0, 3, 0, PortType::Audio, PortType::Audio });
         compileAudioProcessPlan(graph, graph.processPlan);
         engine.prepareForBlock(512, 128, 32);
         engine.prepareRuntimeStates(graph);
         engine.setMidiOutCapture(true);
      }

      float run(int blocks, bool playing)
      {
         graph.transportPlaying = playing;
         float loudest = 0.0f;
         for (int b = 0; b < blocks; ++b)
         {
            float* outputs[2] = { left.data(), right.data() };
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

      std::vector<int> pitches(bool on) const
      {
         std::vector<int> out;
         for (const auto& n : midi)
         {
            if (n.isNoteOn == on)
               out.push_back(n.pitch);
         }
         std::sort(out.begin(), out.end());
         return out;
      }
   };
}

void test_runtime_safety()
{
   printf("\n=== Runtime Safety Nets ===\n");

   // Overflow drops the newest note; the producer never touches the read index.
   {
      AudioGraphEngine idle;
      const int capacity = AudioGraphEngine::kMaxNoteRingCapacity;
      for (int i = 0; i < capacity + 44; ++i)
         idle.queueNote(note(i % 128, true));
      std::vector<AudioGraphEngine::NoteMessage> taken(static_cast<size_t>(capacity));
      const int got = idle.takeQueuedNotes(taken.data(), capacity);
      TEST("Full note ring keeps capacity - 1 notes", got == capacity - 1);
      TEST("Full note ring keeps the oldest notes", got > 0 && taken[0].pitch == 0 && taken[1].pitch == 1);
      TEST("Full note ring counts every dropped note", idle.noteDropCount() == 45);
   }

   // Ten keys pressed and released while stopped, one still held: Play emits no burst.
   {
      PerformanceRig rig;
      rig.run(2, false);
      for (int i = 0; i < 10; ++i)
      {
         rig.engine.queueNote(note(60 + i, true));
         rig.engine.queueNote(note(60 + i, false));
         rig.run(1, false);
      }
      rig.engine.queueNote(note(72, true, 0.7f));
      const float stoppedLevel = rig.run(4, false);
      TEST("Stopped transport is silent (hard mute)", stoppedLevel < 1e-6f);
      TEST("Stopped transport plays no notes", rig.midi.empty());

      rig.run(4, true);
      TEST("Play after stopped keys sends only the held key's note-on", rig.pitches(true) == std::vector<int>({ 72 }));
      // The collapse hands released keys on as note-offs; the filter drops offs for keys it never passed.
      TEST("Keys released while stopped play nothing on Play", rig.pitches(false).empty());

      rig.engine.queueNote(note(72, false));
      rig.run(1, true);
      TEST("The held key releases normally once playing", rig.pitches(false) == std::vector<int>({ 72 }));
   }

   // A host that stops calling processBlock (SDL, main-thread ring): the producer folds notes in,
   // so even hundreds of keys cannot fill the ring, and the first playing block collapses them.
   {
      PerformanceRig rig;
      for (int i = 0; i < 400; ++i)
      {
         rig.engine.queueNote(note(40 + (i / 2) % 20, (i & 1) == 0));
         rig.engine.absorbQueuedNotesWhileStopped();
      }
      rig.engine.queueNote(note(80, true));
      rig.engine.absorbQueuedNotesWhileStopped();
      TEST("Folded stopped notes drop nothing", rig.engine.noteDropCount() == 0);
      rig.run(2, true);
      TEST("First playing block after an idle stop plays only held keys",
           rig.pitches(true) == std::vector<int>({ 80 }));
   }
   {
      // Notes queued before the very first block of a fresh engine collapse as well.
      PerformanceRig rig;
      for (int i = 0; i < 10; ++i)
      {
         rig.engine.queueNote(note(50 + i, true));
         rig.engine.queueNote(note(50 + i, false));
      }
      rig.engine.queueNote(note(64, true));
      const float loudest = rig.run(4, true);
      TEST("Fresh engine: only the held key plays", rig.pitches(true) == std::vector<int>({ 64 }));
      TEST("Fresh engine: the held key is audible", loudest > 0.01f);
   }

   // Hand-off to the worklet engine: folded keys come out as their final state, then the ring.
   {
      AudioGraphEngine idle;
      idle.queueNote(note(60, true, 0.5f));
      idle.queueNote(note(61, true));
      idle.queueNote(note(61, false));
      idle.absorbQueuedNotesWhileStopped();
      idle.queueNote(note(62, true));
      AudioGraphEngine::NoteMessage taken[8];
      const int got = idle.takeQueuedNotes(taken, 8);
      TEST("takeQueuedNotes hands out folded keys then the ring",
           got == 3 && taken[0].pitch == 60 && taken[0].isNoteOn && std::fabs(taken[0].velocity - 0.5f) < 1e-6f &&
           taken[1].pitch == 61 && !taken[1].isNoteOn && taken[2].pitch == 62 && taken[2].isNoteOn);
      TEST("takeQueuedNotes leaves nothing folded behind", idle.takeQueuedNotes(taken, 8) == 0);
   }

   // Fan-in: a port takes any number of cables; the plan sizes its input lists.
   {
      auto mixerPeak = [](int oscillators, int& planInputs, bool& allAccepted)
      {
         ModuleCanvas canvas;
         const int mixer = canvas.createModule("mixer", 400, 100);
         const int output = canvas.createModule("output", 600, 100);
         canvas.setModuleControlValue(output, "limit", 0.0f);
         allAccepted = canvas.connectModules(mixer, 0, output, 0);
         for (int i = 0; i < oscillators; ++i)
         {
            const int osc = canvas.createModule("oscillator", 50, 40.0f * static_cast<float>(i));
            canvas.setModuleControlValue(osc, "frequency", 110.0f);
            canvas.setModuleControlValue(osc, "volume", 0.02f);
            allAccepted = canvas.connectModules(osc, 0, mixer, i % 4) && allAccepted;
         }
         canvas.setTransportPlaying(true);
         AudioGraphSnapshot graph = canvas.createAudioGraphSnapshot();
         planInputs = 0;
         for (const auto& step : graph.processPlan.steps)
         {
            if (step.moduleId == mixer)
               planInputs = static_cast<int>(step.audioInputs.count);
         }
         AudioGraphEngine engine;
         engine.prepareForBlock(512, 128, 32);
         engine.prepareRuntimeStates(graph);
         std::vector<float> left(512), right(512);
         float* outputs[2] = { left.data(), right.data() };
         float peak = 0.0f;
         for (int block = 0; block < 4; ++block)
         {
            engine.processBlock(graph, outputs, 2, 512, 44100.0f);
            for (float v : left)
               peak = std::max(peak, std::fabs(v));
         }
         return peak;
      };
      int oneInput = 0;
      int sixteenInputs = 0;
      bool oneAccepted = false;
      bool sixteenAccepted = false;
      const float onePeak = mixerPeak(1, oneInput, oneAccepted);
      const float sixteenPeak = mixerPeak(16, sixteenInputs, sixteenAccepted);
      TEST("A mixer accepts 16 audio cables", oneAccepted && sixteenAccepted);
      TEST("The plan carries all 16 mixer inputs", oneInput == 1 && sixteenInputs == 16);
      // Identical oscillators in phase sum linearly (limiter off, levels well below clipping).
      TEST("All 16 mixer inputs reach the output",
           onePeak > 1e-4f && std::fabs(sixteenPeak / onePeak - 16.0f) < 0.1f);

      ModuleCanvas canvas;
      const int filter = canvas.createModule("filter", 600, 300);
      int lfoAccepted = 0;
      for (int i = 0; i < 12; ++i)
      {
         const int lfo = canvas.createModule("lfo", 400, 300 + 30.0f * static_cast<float>(i));
         lfoAccepted += canvas.connectModules(lfo, 0, filter, 1) ? 1 : 0;
      }
      int planMods = 0;
      for (const auto& step : canvas.getAudioGraphSnapshot()->processPlan.steps)
      {
         if (step.moduleId == filter)
            planMods = static_cast<int>(step.modulationSources.count);
      }
      TEST("A filter takes 12 modulation cables", lfoAccepted == 12 && planMods == 12);
   }
}
