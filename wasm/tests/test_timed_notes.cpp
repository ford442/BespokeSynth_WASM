// --- Timed notes: NoteMessage::timestamp on a host clock starts a note on its own frame (#126) ---
#include "test_common.h"
#include <cstdlib>
#include <string>

namespace
{
   constexpr float kRate = 48000.0f;
   constexpr int kBlock = 128;
   // The fake host clock starts 1 s in, so a note already rendered still has a positive timestamp.
   constexpr int64_t kStartFrame = 48000;

   double secondsAt(int64_t frame)
   {
      return static_cast<double>(frame) / static_cast<double>(kRate);
   }

   /**
    * A square wave starts at +1 and never crosses 0 between samples: the first non-zero sample is the
    * onset. The output's DC blocker is off so nothing rings on after a note ends.
    */
   AudioGraphSnapshot squareGraph()
   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      appendAudioGraphNode(graph, 1, "oscillator", true,
                           { { "volume", 1.0f }, { "waveform", 2.0f }, { "frequency", 110.0f } });
      appendAudioGraphNode(graph, 2, "output", true, { { "dcblock", 0.0f } });
      graph.connections.push_back(typed_connection(1, 0, 2, 0));
      resolve_port_types(graph, { { 1, "oscillator" }, { 2, "output" } });
      compileAudioProcessPlan(graph, graph.processPlan);
      return graph;
   }

   /** Script module 1 cabled into the square oscillator: its notes go through ScriptNoteQueue. */
   AudioGraphSnapshot scriptSquareGraph()
   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      appendAudioGraphNode(graph, 1, "script", true, {}, { { "script", "" } });
      appendAudioGraphNode(graph, 2, "oscillator", true,
                           { { "volume", 1.0f }, { "waveform", 2.0f }, { "frequency", 110.0f } });
      appendAudioGraphNode(graph, 3, "output", true, { { "dcblock", 0.0f } });
      graph.connections.push_back(typed_connection(1, 0, 2, 0));
      graph.connections.push_back(typed_connection(2, 0, 3, 0));
      resolve_port_types(graph, { { 1, "script" }, { 2, "oscillator" }, { 3, "output" } });
      compileAudioProcessPlan(graph, graph.processPlan);
      return graph;
   }

   /**
    * One engine on a fake host clock. An uncabled oscillator free-runs until its first note, so
    * primer blocks play (and end) a note first; `left` and at() count from the block after.
    */
   struct TimedRig
   {
      AudioGraphSnapshot graph;
      AudioGraphEngine engine;
      int64_t frame = kStartFrame;
      int64_t origin = 0;
      std::vector<float> left; // every block rendered after the primer

      explicit TimedRig(AudioGraphSnapshot snapshot)
      : graph(std::move(snapshot))
      {
         engine.prepareForBlock(kBlock, 128, 32);
         engine.prepareRuntimeStates(graph);
         // The first playing block folds what was queued before it to one event per key.
         render(1);
         note(1, true, 0.0);
         note(1, false, 0.0);
         render(1);
         left.clear();
         origin = frame;
      }

      /** Host time of `offset` frames after the first block `left` holds. */
      double at(int64_t offset) const { return secondsAt(origin + offset); }

      void note(int pitch, bool on, double timestamp, int scriptModuleId = -1)
      {
         AudioGraphEngine::NoteMessage message;
         message.pitch = pitch;
         message.velocity = on ? 1.0f : 0.0f;
         message.isNoteOn = on;
         message.timestamp = timestamp;
         message.scriptModuleId = scriptModuleId;
         engine.queueNote(message);
      }

      void render(int blocks, bool clocked = true)
      {
         std::vector<float> l(kBlock), r(kBlock);
         float* outputs[2] = { l.data(), r.data() };
         for (int b = 0; b < blocks; ++b)
         {
            engine.processBlock(graph, outputs, 2, kBlock, kRate, clocked ? secondsAt(frame) : -1.0);
            left.insert(left.end(), l.begin(), l.end());
            frame += kBlock;
         }
      }

      /** Index into `left` of the first non-silent sample, or -1. */
      int firstSound(int from = 0) const
      {
         for (size_t i = static_cast<size_t>(from); i < left.size(); ++i)
         {
            if (std::fabs(left[i]) > 1e-6f)
               return static_cast<int>(i);
         }
         return -1;
      }

      /** Index of the last non-silent sample, or -1. */
      int lastSound() const
      {
         for (size_t i = left.size(); i > 0; --i)
         {
            if (std::fabs(left[i - 1]) > 1e-6f)
               return static_cast<int>(i - 1);
         }
         return -1;
      }

      float peakFrom(int from) const
      {
         float peak = 0.0f;
         for (size_t i = static_cast<size_t>(from); i < left.size(); ++i)
            peak = std::max(peak, std::fabs(left[i]));
         return peak;
      }
   };

   bool within(int actual, int expected, int tolerance)
   {
      return actual >= 0 && std::abs(actual - expected) <= tolerance;
   }
}

void test_timed_notes()
{
   printf("\n=== Timed Note Tests ===\n");

   // Baseline: where an untimed note sounds in the first block (the graph's own latency, if any).
   int base = -1;
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, 0.0);
      rig.render(2);
      base = rig.firstSound();
      TEST("Untimed note starts in the block it is queued for", base >= 0 && base < kBlock);
   }

   // Same block, the next one and seven blocks out; offsets on and next to block boundaries.
   bool allOnFrame = true;
   std::string misses;
   for (int offset : { 1, 37, 64, 127, 128, 129, 300, 1000 })
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(offset));
      rig.render(offset / kBlock + 3);
      const int onset = rig.firstSound();
      if (!within(onset, base + offset, 1))
      {
         allOnFrame = false;
         misses += " " + std::to_string(offset) + "->" + std::to_string(onset - base);
      }
   }
   if (!allOnFrame)
      printf("  timed onsets off by more than a sample:%s\n", misses.c_str());
   TEST("Timed note-on starts within ±1 sample of its frame (8 offsets, this block and later ones)", allOnFrame);

   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(37));
      rig.render(3);
      const auto stats = rig.engine.stats();
      TEST("A mid-block note splits that block once and counts as timed, not late",
           stats.timedNotes == 1 && stats.lateNotes == 0 && stats.splitBlocks == 1);
   }
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(2 * kBlock));
      rig.render(4);
      TEST("A note on a block boundary renders that block whole", rig.engine.stats().splitBlocks == 0 &&
                                                                  within(rig.firstSound(), base + 2 * kBlock, 1));
   }
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(10));
      rig.note(60, false, rig.at(74));
      rig.render(3);
      TEST("Timed note-off ends the note on its frame (±1)",
           within(rig.firstSound(), base + 10, 1) && within(rig.lastSound(), base + 73, 1));
   }

   // Late, too far ahead, and no host clock: all play at the next block start.
   {
      TimedRig rig(squareGraph());
      rig.render(2);
      rig.note(60, true, rig.at(100));
      rig.render(2);
      const auto stats = rig.engine.stats();
      TEST("A note whose frame was already rendered plays at the next block start, counted late",
           within(rig.firstSound(), 2 * kBlock + base, 0) && stats.timedNotes == 1 && stats.lateNotes == 1);
   }
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(0) + 2.0 * AudioGraphEngine::kMaxScheduleAheadSeconds);
      rig.render(2);
      TEST("A timestamp beyond the schedule horizon plays now", within(rig.firstSound(), base, 0));
   }
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(300));
      rig.render(2, false);
      TEST("Without a host clock a timed note plays at the block start",
           within(rig.firstSound(), base, 0) && rig.engine.stats().timedNotes == 0);
   }
   {
      TimedRig clocked(squareGraph());
      TimedRig unclocked(squareGraph());
      for (TimedRig* rig : { &clocked, &unclocked })
      {
         rig->note(60, true, 0.0);
         rig->note(67, true, 0.0);
      }
      clocked.render(4);
      unclocked.render(4, false);
      for (TimedRig* rig : { &clocked, &unclocked })
         rig->note(60, false, 0.0);
      clocked.render(2);
      unclocked.render(2, false);
      TEST("Untimed notes render bit-identically with and without a host clock",
           clocked.left == unclocked.left && clocked.engine.stats().splitBlocks == 0);
   }

   // A note never overtakes an earlier one for the same key.
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(100));
      rig.note(60, false, rig.at(50));
      rig.render(3);
      TEST("A note-off stamped before its note-on still ends it", rig.peakFrom(0) == 0.0f);
   }
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(300));
      rig.note(60, false, 0.0);
      rig.render(5);
      TEST("An untimed note-off waits behind a scheduled note-on for the same key", rig.peakFrom(0) == 0.0f);
   }
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(300));
      rig.note(64, true, 0.0);
      rig.render(1);
      TEST("Other keys do not wait behind a scheduled note", within(rig.firstSound(), base, 0));
   }

   // Pending notes leave with the transport, a reset and a handover.
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(10));
      rig.note(60, false, rig.at(1000));
      rig.render(2);
      rig.graph.transportPlaying = false;
      rig.render(1);
      rig.graph.transportPlaying = true;
      const int resumed = static_cast<int>(rig.left.size());
      rig.render(10);
      TEST("A pending note-off survives a transport stop (no stuck note on Play)", rig.peakFrom(resumed) == 0.0f);
   }
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(1000));
      rig.render(1);
      rig.engine.resetClock();
      AudioGraphEngine::NoteMessage taken[4];
      const bool empty = rig.engine.takeQueuedNotes(taken, 4) == 0;
      rig.render(10);
      TEST("resetClock drops notes waiting for their block", empty && rig.peakFrom(0) == 0.0f);
   }
   {
      TimedRig rig(squareGraph());
      rig.note(60, true, rig.at(1000));
      rig.render(1);
      AudioGraphEngine::NoteMessage taken[4];
      const int got = rig.engine.takeQueuedNotes(taken, 4);
      TEST("takeQueuedNotes hands over a waiting note with its timestamp",
           got == 1 && taken[0].pitch == 60 && taken[0].isNoteOn && taken[0].timestamp == rig.at(1000));
   }

   // A note for a Script module's cables is queued for it on its frame.
   {
      resetScriptNoteQueues();
      int scriptBase = -1;
      {
         TimedRig rig(scriptSquareGraph());
         rig.note(60, true, 0.0, 1);
         rig.render(2);
         scriptBase = rig.firstSound();
      }
      TimedRig rig(scriptSquareGraph());
      rig.note(60, true, rig.at(37), 1);
      rig.note(60, false, rig.at(200), 1);
      rig.render(3);
      TEST("A timed note for a Script module's cables plays through it on its frame (±1)",
           scriptBase >= 0 && within(rig.firstSound(), scriptBase + 37, 1) &&
           within(rig.lastSound(), scriptBase + 199, 1));
      resetScriptNoteQueues();
   }

   // The split path stays real-time safe on a patch that reads samples: under BESPOKE_WASM_RT_ASSERT
   // any allocation or RtMutex lock (SampleStore, ModuleCanvas) inside processBlock aborts.
   {
      auto& store = SampleStore::instance();
      const int sampleId = store.count();
      std::vector<float> pcm(4800, 0.5f);
      store.adopt(sampleId, std::make_shared<SampleBuffer>(pcm, 1, 48000, "timed-rt-sample", "rt.wav"));
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      appendAudioGraphNode(graph, 1, "sampler", true, { { "volume", 1.0f }, { "mode", 0.0f } },
                           { { "sampleHash", "timed-rt-sample" } });
      appendAudioGraphNode(graph, 2, "looper", true, { { "mix", 1.0f } });
      appendAudioGraphNode(graph, 3, "output", true, { { "dcblock", 0.0f } });
      graph.connections.push_back(typed_connection(1, 0, 2, 0));
      graph.connections.push_back(typed_connection(2, 0, 3, 0));
      graph.connections.push_back(typed_connection(1, 0, 3, 0)); // the looper at mix 1 plays only its loop
      resolve_port_types(graph, { { 1, "sampler" }, { 2, "looper" }, { 3, "output" } });
      compileAudioProcessPlan(graph, graph.processPlan);
      TimedRig rig(std::move(graph));
      for (int i = 0; i < 16; ++i)
         rig.note(60 + (i % 4), (i % 2) == 0, rig.at(17 + i * 41));
      rig.render(8);
      TEST("Split blocks over a sampler and looper take no lock and allocate nothing",
           rig.engine.stats().splitBlocks > 0 && rig.peakFrom(0) > 0.1f && audioRtViolations().locks == 0);
      store.evict(sampleId);
   }
   {
      RtMutex mutex;
      mutex.lock();
      mutex.unlock();
      TEST("RtMutex outside an audio block is not a violation",
           audioRtViolations().locks == 0 && audioRtViolations().countsAllocations);
   }
}
