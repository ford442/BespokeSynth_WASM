// --- AudioWorklet engine (Option A): snapshot wire format and the worklet C ABI ---
#include "test_common.h"
#include "BespokeWasm/AudioGraphSnapshotCodec.h"
#include "BespokeWasm/AudioWorkletEngine.h"
#include "BespokeWasm/LooperArena.h"
#include <cstddef>
#include <string>

// AudioWorkletEngine.cpp (linked here; in the app only BespokeSynthWASM_audio has it).
extern "C" {
int bespoke_audio_wire_abi(void);
int bespoke_audio_init(int sampleRate, int maxBlock);
int bespoke_audio_apply_snapshot(const unsigned char* bytes, int length, int reset);
const char* bespoke_audio_last_error(void);
int bespoke_audio_adopt_sample(int id, int channels, int frames, int sampleRate, const float* planar, int generation);
void bespoke_audio_evict_sample(int id);
void bespoke_audio_apply_events(const int* words, int count);
void bespoke_audio_process(float* left, float* right, int frames, double blockFrame);
void bespoke_audio_read_status(double* out, int count);
void bespoke_audio_set_midi_out_capture(int enabled);
int bespoke_audio_drain_midi_out(int* out, int maxEvents);
}

namespace
{
   struct Rendered
   {
      std::vector<float> left;
      std::vector<float> right;
   };

   Rendered renderWith(const AudioGraphSnapshot& graph, int blocks, int notePitch = -1, int blockSize = 256)
   {
      AudioGraphEngine engine;
      engine.prepareForBlock(blockSize, 128, 32);
      engine.prepareRuntimeStates(graph);
      if (notePitch >= 0)
      {
         AudioGraphEngine::NoteMessage note;
         note.pitch = notePitch;
         note.velocity = 1.0f;
         note.isNoteOn = true;
         engine.queueNote(note);
      }
      Rendered out;
      std::vector<float> left(static_cast<size_t>(blockSize));
      std::vector<float> right(static_cast<size_t>(blockSize));
      for (int b = 0; b < blocks; ++b)
      {
         float* outputs[2] = { left.data(), right.data() };
         engine.processBlock(graph, outputs, 2, blockSize, 44100.0f);
         out.left.insert(out.left.end(), left.begin(), left.end());
         out.right.insert(out.right.end(), right.begin(), right.end());
      }
      return out;
   }

   float peakOf(const std::vector<float>& buffer)
   {
      float peak = 0.0f;
      for (float v : buffer)
         peak = std::max(peak, std::fabs(v));
      return peak;
   }

   std::vector<float> sinePlanar(int frames, float hz, float amplitude)
   {
      std::vector<float> pcm(static_cast<size_t>(frames));
      for (int i = 0; i < frames; ++i)
         pcm[static_cast<size_t>(i)] = amplitude * std::sin(6.28318530718f * hz * i / 44100.0f);
      return pcm;
   }

   /** Oscillator → filter (LFO on cutoff) → gain → delay → output, plus sampler and looper. */
   AudioGraphSnapshot makeWorkletGraph(const std::string& sampleHash)
   {
      AudioGraphSnapshot graph = make_test_graph(0.6f, 1500.0f, 0.3f);
      graph.transportBPM = 133.0f;
      appendAudioGraphNode(graph, 6, "sampler", true, { { "volume", 1.0f }, { "mode", 0.0f } },
                           { { "sampleHash", sampleHash } });
      appendAudioGraphNode(graph, 7, "delay", true, { { "time", 0.05f }, { "feedback", 0.3f }, { "mix", 0.4f } });
      appendAudioGraphNode(graph, 8, "looper", true, { { "mix", 1.0f } });
      graph.connections.push_back(typed_connection(6, 0, 7, 0));
      graph.connections.push_back(typed_connection(7, 0, 5, 0));
      graph.connections.push_back(typed_connection(3, 0, 8, 0));
      graph.connections.push_back(typed_connection(8, 0, 5, 0));
      resolve_port_types(graph, { { 1, "oscillator" }, { 2, "filter" }, { 3, "gain" }, { 4, "lfo" }, { 5, "output" }, { 6, "sampler" }, { 7, "delay" }, { 8, "looper" } });
      compileAudioProcessPlan(graph, graph.processPlan);
      return graph;
   }

   const AudioGraphNode* nodeById(const AudioGraphSnapshot& graph, int id)
   {
      for (const auto& node : graph.nodes)
      {
         if (node.id == id)
            return &node;
      }
      return nullptr;
   }

   template <typename T>
   T readAt(const std::vector<uint8_t>& wire, size_t pos)
   {
      T value{};
      std::memcpy(&value, wire.data() + pos, sizeof(T));
      return value;
   }

   template <typename T>
   void writeAt(std::vector<uint8_t>& wire, size_t pos, T value)
   {
      std::memcpy(wire.data() + pos, &value, sizeof(T));
   }

   /**
    * Rewrites every type index on the wire to 1000 + k, as a build whose registry ordered its
    * adapters differently would send them. Layout: AudioGraphSnapshotCodec.cpp.
    */
   void scrambleWireTypeIndices(std::vector<uint8_t>& wire)
   {
      size_t pos = 4 + 4 + sizeof(uint64_t) + sizeof(uint32_t) + 1 + sizeof(float) + sizeof(GraphMusicScale);
      const uint32_t typeCount = readAt<uint32_t>(wire, pos);
      pos += 4;
      std::map<int32_t, int32_t> remap;
      for (uint32_t k = 0; k < typeCount; ++k)
      {
         const int32_t fake = 1000 + static_cast<int32_t>(k);
         remap[readAt<int32_t>(wire, pos)] = fake;
         writeAt(wire, pos, fake);
         pos += 4;
         pos += 4 + readAt<uint32_t>(wire, pos);
      }
      const uint32_t nodeCount = readAt<uint32_t>(wire, pos);
      pos += 4;
      for (uint32_t i = 0; i < nodeCount; ++i)
      {
         const size_t field = pos + i * sizeof(AudioGraphNode) + offsetof(AudioGraphNode, typeIndex);
         writeAt(wire, field, remap[readAt<int32_t>(wire, field)]);
      }
      pos += nodeCount * sizeof(AudioGraphNode);
      pos += 4 + readAt<uint32_t>(wire, pos) * sizeof(AudioGraphConnection);
      pos += 4 + readAt<uint32_t>(wire, pos);
      pos += 3 + 5 * 4; // plan flags and scalars
      const uint32_t stepCount = readAt<uint32_t>(wire, pos);
      pos += 4;
      for (uint32_t i = 0; i < stepCount; ++i)
      {
         const size_t field = pos + i * sizeof(PlanStep) + offsetof(PlanStep, typeIndex);
         writeAt(wire, field, remap[readAt<int32_t>(wire, field)]);
      }
   }
}

void test_audio_graph_snapshot_codec()
{
   printf("\n=== Audio Graph Snapshot Codec Tests ===\n");

   auto& store = SampleStore::instance();
   const int sampleId = store.count();
   const std::vector<float> sine = sinePlanar(2205, 440.0f, 0.5f);
   store.adopt(sampleId, std::make_shared<SampleBuffer>(sine, 1, 44100, "codec-test-a", "a.wav"));

   const AudioGraphSnapshot graph = makeWorkletGraph("codec-test-a");
   TEST("Worklet test graph compiles", graph.processPlan.valid && graph.nodes.size() == 8);

   std::vector<uint8_t> wire;
   encodeAudioGraphSnapshot(graph, wire);
   AudioGraphSnapshot decoded;
   std::string error;
   TEST("Snapshot decodes", decodeAudioGraphSnapshot(wire.data(), wire.size(), decoded, error));
   TEST("Nodes, cables and params cross intact",
        decoded.nodes.size() == graph.nodes.size() && decoded.connections.size() == graph.connections.size() &&
        decoded.paramArena.size() == graph.paramArena.size());
   TEST("Transport and tempo cross", decoded.transportPlaying && decoded.transportBPM == 133.0f);
   const AudioProcessPlan& a = graph.processPlan;
   const AudioProcessPlan& b = decoded.processPlan;
   TEST("Process plan crosses", b.valid && b.steps.size() == a.steps.size() && b.bufferSlotCount == a.bufferSlotCount &&
                                b.outputNodeIndex == a.outputNodeIndex && b.noteSourcePlans.size() == a.noteSourcePlans.size() &&
                                b.nodeIndexByModuleId == a.nodeIndexByModuleId);
   std::vector<uint8_t> again;
   encodeAudioGraphSnapshot(decoded, again);
   TEST("Re-encoding a decoded snapshot is byte-identical", again == wire);

   const Rendered original = renderWith(graph, 12, 60);
   const Rendered remote = renderWith(decoded, 12, 60);
   TEST("Decoded graph renders bit-identically (osc, LFO, sampler, delay, looper)",
        original.left == remote.left && original.right == remote.right && peakOf(original.left) > 0.05f);

   const auto* looper = nodeById(decoded, 8);
   const auto* looperParams = looper ? static_cast<const LooperParams*>(decoded.paramsFor(*looper)) : nullptr;
   TEST("Looper params rebind to this instance's arena",
        looperParams && looperParams->arena && looperParams->arena == LooperArenaPool::instance().find(8));

   // Type indices travel as names: a sender with another registry order still lands on the right adapters.
   {
      std::vector<uint8_t> scrambled = wire;
      scrambleWireTypeIndices(scrambled);
      AudioGraphSnapshot remapped;
      const bool ok = decodeAudioGraphSnapshot(scrambled.data(), scrambled.size(), remapped, error);
      bool sameTypes = ok && remapped.nodes.size() == graph.nodes.size();
      for (size_t i = 0; sameTypes && i < graph.nodes.size(); ++i)
         sameTypes = remapped.nodes[i].typeIndex == graph.nodes[i].typeIndex;
      for (size_t i = 0; sameTypes && i < graph.processPlan.steps.size(); ++i)
         sameTypes = remapped.processPlan.steps[i].typeIndex == graph.processPlan.steps[i].typeIndex;
      TEST("Remote type indices are remapped by type id (nodes and plan steps)", sameTypes);
   }

   // A type this build lacks plays silence instead of failing the whole patch.
   {
      std::vector<uint8_t> unknown = wire;
      const std::string name = "oscillator";
      auto it = std::search(unknown.begin(), unknown.end(), name.begin(), name.end());
      if (it != unknown.end())
         *(it + static_cast<std::ptrdiff_t>(name.size()) - 1) = 'X';
      AudioGraphSnapshot partial;
      const bool ok = it != unknown.end() && decodeAudioGraphSnapshot(unknown.data(), unknown.size(), partial, error);
      const auto* osc = ok ? nodeById(partial, 1) : nullptr;
      const auto* filter = ok ? nodeById(partial, 2) : nullptr;
      TEST("Unknown type id decodes as a silent node", osc && osc->typeIndex == -1 && filter && filter->typeIndex >= 0);
   }

   {
      std::vector<uint8_t> bad = wire;
      bad[0] ^= 0xFF;
      AudioGraphSnapshot ignored;
      TEST("Wrong magic is rejected", !decodeAudioGraphSnapshot(bad.data(), bad.size(), ignored, error) &&
                                      error.find("not an audio graph") != std::string::npos);
      bad = wire;
      bad[4] ^= 0x01;
      TEST("ABI mismatch is rejected", !decodeAudioGraphSnapshot(bad.data(), bad.size(), ignored, error) &&
                                       error.find("ABI") != std::string::npos);
      TEST("ABI is stable within a build", audioGraphWireAbi() == static_cast<uint32_t>(bespoke_audio_wire_abi()));
   }

   {
      bool everyPrefixRejected = true;
      for (size_t length = 0; length < wire.size() && everyPrefixRejected; ++length)
      {
         AudioGraphSnapshot ignored;
         everyPrefixRejected = !decodeAudioGraphSnapshot(wire.data(), length, ignored, error);
      }
      TEST("Every truncated prefix is rejected", everyPrefixRejected);
   }

   {
      // A node index past the end must never reach the engine.
      AudioGraphSnapshot broken = graph;
      broken.processPlan.steps[0].nodeIndex = static_cast<int>(broken.nodes.size()) + 3;
      std::vector<uint8_t> brokenWire;
      encodeAudioGraphSnapshot(broken, brokenWire);
      AudioGraphSnapshot ignored;
      const bool rejectedIndex = !decodeAudioGraphSnapshot(brokenWire.data(), brokenWire.size(), ignored, error);

      broken = graph;
      broken.processPlan.steps[0].audioInputs = { 0, 99 };
      encodeAudioGraphSnapshot(broken, brokenWire);
      const bool rejectedCount = !decodeAudioGraphSnapshot(brokenWire.data(), brokenWire.size(), ignored, error);
      TEST("Out-of-range plan indices and input ranges are rejected",
           rejectedIndex && rejectedCount && error.find("malformed") != std::string::npos);
   }

   // A real canvas graph: transport and scale are param-less nodes whose aligned offset can sit past
   // the arena end. They must not read as a params mismatch.
   {
      ModuleCanvas canvas;
      const int osc = canvas.createModule("oscillator", 0, 0);
      const int delay = canvas.createModule("delay", 100, 0);
      const int out = canvas.createModule("output", 200, 0);
      canvas.connectModules(osc, 0, delay, 0);
      canvas.connectModules(delay, 0, out, 0);
      canvas.setTransportPlaying(true);
      const auto published = canvas.getAudioGraphSnapshot();
      bool paramlessPastEnd = false;
      for (const auto& node : published->nodes)
         paramlessPastEnd = paramlessPastEnd || (node.paramSize == 0 && node.paramOffset > published->paramArena.size());
      std::vector<uint8_t> canvasWire;
      encodeAudioGraphSnapshot(*published, canvasWire);
      AudioGraphSnapshot fromCanvas;
      const bool ok = decodeAudioGraphSnapshot(canvasWire.data(), canvasWire.size(), fromCanvas, error);
      if (!ok)
         printf("  decode error: %s\n", error.c_str());
      TEST("A canvas snapshot with param-less nodes decodes",
           paramlessPastEnd && ok && fromCanvas.nodes.size() == published->nodes.size());
      const Rendered local = renderWith(*published, 6);
      const Rendered remote = renderWith(fromCanvas, 6);
      TEST("Canvas snapshot renders identically after the round trip",
           local.left == remote.left && peakOf(local.left) > 0.01f);
   }

   // Sampler params name their sample by handle (id + generation); the receiver resolves it in its own store.
   {
      const uint32_t sentGeneration = store.generationOf(sampleId);
      const std::vector<float> other = sinePlanar(2205, 220.0f, 0.25f);
      store.adopt(sampleId, std::make_shared<SampleBuffer>(other, 1, 44100, "codec-test-b", "b.wav"), sentGeneration);
      AudioGraphSnapshot rebound;
      const bool ok = decodeAudioGraphSnapshot(wire.data(), wire.size(), rebound, error);
      const auto* sampler = ok ? nodeById(rebound, 6) : nullptr;
      const auto* params = sampler ? static_cast<const SamplerParams*>(rebound.paramsFor(*sampler)) : nullptr;
      TEST("Sampler params rebind by sample handle, not the sender's pointer",
           params && params->handle.index == sampleId && params->sample == store.findById(sampleId) &&
           params->sample->hash() == "codec-test-b");

      // A buffer adopted under another generation is a different sample: the handle no longer resolves.
      store.adopt(sampleId, std::make_shared<SampleBuffer>(other, 1, 44100, "codec-test-c", "c.wav"));
      AudioGraphSnapshot stale;
      const bool staleOk = decodeAudioGraphSnapshot(wire.data(), wire.size(), stale, error);
      const auto* staleSampler = staleOk ? nodeById(stale, 6) : nullptr;
      const auto* staleParams =
      staleSampler ? static_cast<const SamplerParams*>(stale.paramsFor(*staleSampler)) : nullptr;
      TEST("A stale sample handle resolves to no buffer", staleParams && staleParams->sample == nullptr);
   }
}

void test_audio_worklet_engine_abi()
{
   printf("\n=== AudioWorklet Engine C ABI Tests ===\n");

   auto& store = SampleStore::instance();
   const int previousDeviceRate = store.deviceSampleRate();
   constexpr int kQuantum = 128;
   std::vector<float> left(kQuantum), right(kQuantum);
   std::string error;

   // Handover: notes the UI module's engine queued but never rendered move to the worklet in order.
   {
      AudioGraphEngine idle;
      for (int pitch : { 60, 64, 67 })
      {
         AudioGraphEngine::NoteMessage note;
         note.pitch = pitch;
         note.velocity = 0.9f;
         note.isNoteOn = true;
         idle.queueNote(note);
      }
      AudioGraphEngine::NoteMessage taken[8];
      const int got = idle.takeQueuedNotes(taken, 8);
      TEST("takeQueuedNotes pops unrendered notes oldest first",
           got == 3 && taken[0].pitch == 60 && taken[1].pitch == 64 && taken[2].pitch == 67 && taken[2].isNoteOn);
      TEST("takeQueuedNotes leaves the queue empty", idle.takeQueuedNotes(taken, 8) == 0);
   }

   TEST("bespoke_audio_init succeeds", bespoke_audio_init(44100, 2048) == 0);
   bespoke_audio_process(left.data(), right.data(), kQuantum, -1.0);
   TEST("No snapshot yet: silence", peakOf(left) == 0.0f && peakOf(right) == 0.0f);

   const std::vector<uint8_t> junk(64, 0x55);
   TEST("A junk snapshot is rejected with a reason",
        bespoke_audio_apply_snapshot(junk.data(), static_cast<int>(junk.size()), 0) == -1 &&
        std::string(bespoke_audio_last_error()).find("not an audio graph") != std::string::npos);

   // The UI side builds the graph against its decoded sample, then the worklet adopts its own copy.
   const int sampleId = store.count();
   const std::vector<float> sine = sinePlanar(4410, 440.0f, 0.5f);
   store.adopt(sampleId, std::make_shared<SampleBuffer>(sine, 1, 44100, "worklet-abi-sample", "s.wav"));
   AudioGraphSnapshot graph;
   graph.transportPlaying = true;
   appendAudioGraphNode(graph, 1, "sampler", true, { { "volume", 1.0f }, { "mode", 0.0f } },
                        { { "sampleHash", "worklet-abi-sample" } });
   appendAudioGraphNode(graph, 2, "output", true, {});
   graph.connections.push_back(typed_connection(1, 0, 2, 0));
   resolve_port_types(graph, { { 1, "sampler" }, { 2, "output" } });
   compileAudioProcessPlan(graph, graph.processPlan);
   std::vector<uint8_t> wire;
   encodeAudioGraphSnapshot(graph, wire);

   const int sentGeneration = static_cast<int>(store.generationOf(sampleId));
   TEST("Sample copy is adopted under the sender's id",
        bespoke_audio_adopt_sample(sampleId, 1, 4410, 44100, sine.data(), sentGeneration) == 0 &&
        store.findById(sampleId) && store.findById(sampleId)->hash().empty() &&
        store.generationOf(sampleId) == static_cast<uint32_t>(sentGeneration));
   TEST("Snapshot applies", bespoke_audio_apply_snapshot(wire.data(), static_cast<int>(wire.size()), 0) == 0);

   bespoke_audio_process(left.data(), right.data(), kQuantum, -1.0);
   TEST("Sampler waits for a note", peakOf(left) == 0.0f);

   auto floatBits = [](float value)
   {
      int bits = 0;
      std::memcpy(&bits, &value, sizeof(bits));
      return bits;
   };
   const int noteOn[4] = { static_cast<int>(AudioEngineEventType::Note), 60 | (1 << 12), floatBits(1.0f), 0 };
   bespoke_audio_apply_events(noteOn, 1);
   float peak = 0.0f;
   for (int i = 0; i < 8; ++i)
   {
      bespoke_audio_process(left.data(), right.data(), kQuantum, -1.0);
      peak = std::max(peak, peakOf(left));
   }
   TEST("Note event plays the adopted sample", peak > 0.2f);

   double status[kStatusSlotCount]{};
   bespoke_audio_read_status(status, kStatusSlotCount);
   TEST("Status reports callbacks, snapshots, peaks and a moving beat",
        status[kStatusCallbackCount] >= 9 && status[kStatusSnapshotCount] == 1 && status[kStatusOutputPeakL] > 0.2f &&
        status[kStatusBeat] > 0.0);
   double again[kStatusSlotCount]{};
   bespoke_audio_read_status(again, kStatusSlotCount);
   TEST("Reading status takes the peaks", again[kStatusOutputPeakL] == 0.0);

   TEST("A reset snapshot restarts the clock",
        bespoke_audio_apply_snapshot(wire.data(), static_cast<int>(wire.size()), 1) == 0);
   bespoke_audio_read_status(status, kStatusSlotCount);
   TEST("Beat is back at zero after reset", status[kStatusBeat] == 0.0 && status[kStatusSnapshotCount] == 2);

   // Script notes and clears reach the per-module queues the Script adapter drains.
   {
      const int scriptModule = 4242;
      const int scriptNote[8] = { static_cast<int>(AudioEngineEventType::ScriptNote), scriptModule, 64 | (1 << 8),
                                  floatBits(0.5f), static_cast<int>(AudioEngineEventType::ScriptNote), scriptModule,
                                  67 | (1 << 8), floatBits(0.75f) };
      bespoke_audio_apply_events(scriptNote, 2);
      WasmNoteEvent drained[4];
      const int got = drainScriptNotes(scriptModule, drained, 4);
      TEST("Script note events queue for their module",
           got == 2 && drained[0].pitch == 64 && drained[0].isNoteOn && drained[0].velocity == 0.5f &&
           drained[1].pitch == 67);
      bespoke_audio_apply_events(scriptNote, 1);
      const int clear[4] = { static_cast<int>(AudioEngineEventType::ScriptClear), scriptModule, 0, 0 };
      bespoke_audio_apply_events(clear, 1);
      TEST("Script clear drops queued notes", drainScriptNotes(scriptModule, drained, 4) == 0);
   }

   std::vector<float> bigLeft(4096, 1.0f), bigRight(4096, 1.0f);
   bespoke_audio_apply_events(noteOn, 1);
   bespoke_audio_process(bigLeft.data(), bigRight.data(), 4096, -1.0);
   TEST("A block larger than the init size renders silence instead of overrunning",
        peakOf(bigLeft) == 0.0f && peakOf(bigRight) == 0.0f);

   // Timed notes and Script worker notes on the AudioContext frame clock.
   {
      // Square oscillators start at +1, so the first non-zero sample is the onset (DC blocker off:
      // no tail after a note ends).
      const WasmControlMap squareParams{ { "volume", 1.0f }, { "waveform", 2.0f } };
      AudioGraphSnapshot keyboard; // Script 5 has no cables; oscillator 1 plays performance notes
      keyboard.transportPlaying = true;
      appendAudioGraphNode(keyboard, 5, "script", true, {}, { { "script", "" } });
      appendAudioGraphNode(keyboard, 1, "oscillator", true, squareParams);
      appendAudioGraphNode(keyboard, 2, "output", true, { { "dcblock", 0.0f } });
      keyboard.connections.push_back(typed_connection(1, 0, 2, 0));
      resolve_port_types(keyboard, { { 1, "oscillator" }, { 2, "output" } });
      compileAudioProcessPlan(keyboard, keyboard.processPlan);
      AudioGraphSnapshot cabled; // Script 5 → oscillator 6 → output
      cabled.transportPlaying = true;
      appendAudioGraphNode(cabled, 5, "script", true, {}, { { "script", "" } });
      appendAudioGraphNode(cabled, 6, "oscillator", true, squareParams);
      appendAudioGraphNode(cabled, 7, "output", true, { { "dcblock", 0.0f } });
      cabled.connections.push_back(typed_connection(5, 0, 6, 0));
      cabled.connections.push_back(typed_connection(6, 0, 7, 0));
      resolve_port_types(cabled, { { 5, "script" }, { 6, "oscillator" }, { 7, "output" } });
      compileAudioProcessPlan(cabled, cabled.processPlan);
      std::vector<uint8_t> keyboardWire, cabledWire;
      encodeAudioGraphSnapshot(keyboard, keyboardWire);
      encodeAudioGraphSnapshot(cabled, cabledWire);

      // The clock starts three quanta before frame 2^32, so a start frame's low 32 bits wrap.
      double blockFrame = 4294967296.0 - 3 * kQuantum;
      std::vector<float> heard;
      auto render = [&](int blocks)
      {
         heard.clear();
         for (int b = 0; b < blocks; ++b)
         {
            bespoke_audio_process(left.data(), right.data(), kQuantum, blockFrame);
            heard.insert(heard.end(), left.begin(), left.end());
            blockFrame += kQuantum;
         }
      };
      auto onset = [&]()
      {
         for (size_t i = 0; i < heard.size(); ++i)
         {
            if (std::fabs(heard[i]) > 1e-6f)
               return static_cast<int>(i);
         }
         return -1;
      };
      auto lastBlockSilent = [&]()
      {
         return std::all_of(heard.end() - kQuantum, heard.end(), [](float v)
                            {
                               return v == 0.0f;
                            });
      };
      auto note = [&](int pitch, bool on, int64_t frame)
      {
         const int timed = frame >= 0 ? kAudioEngineNoteTimed : 0;
         const int record[4] = { static_cast<int>(AudioEngineEventType::Note), pitch | ((on ? 1 : 0) << 12) | timed,
                                 floatBits(1.0f), static_cast<int>(static_cast<uint32_t>(frame)) };
         bespoke_audio_apply_events(record, 1);
      };
      auto directNote = [&](int moduleId, int pitch, bool on, int64_t frame)
      {
         const auto packed = static_cast<uint32_t>((moduleId + 1) & 0xFFFFF) | static_cast<uint32_t>(pitch) << 20 |
                             static_cast<uint32_t>(on ? 1 : 0) << 27 |
                             static_cast<uint32_t>(frame >= 0 ? kAudioEngineScriptDirectTimed : 0);
         const int record[4] = { static_cast<int>(AudioEngineEventType::ScriptDirectNote), static_cast<int>(packed),
                                 floatBits(0.8f), static_cast<int>(static_cast<uint32_t>(frame)) };
         bespoke_audio_apply_events(record, 1);
      };
      const int scriptClear[4] = { static_cast<int>(AudioEngineEventType::ScriptClear), 5, 0, 0 };

      TEST("Keyboard test graph applies",
           bespoke_audio_apply_snapshot(keyboardWire.data(), static_cast<int>(keyboardWire.size()), 1) == 0);
      // An uncabled oscillator free-runs until its first note: play and end one first (after
      // the first playing block, which folds earlier notes to one event per key).
      render(1);
      note(1, true, -1);
      note(1, false, -1);
      render(1);
      note(60, true, -1);
      render(1);
      const int base = onset();
      note(60, false, -1);
      render(1);
      TEST("Untimed Note record still plays at the block start", base >= 0 && base < kQuantum && lastBlockSilent());

      const int64_t next = static_cast<int64_t>(blockFrame);
      note(60, true, next + kQuantum + 8); // frame 2^32 + 8: its low word is 8
      render(2);
      TEST("Timed Note record starts on its AudioContext frame across the 2^32 wrap (±1)",
           std::abs(onset() - (base + kQuantum + 8)) <= 1);
      note(60, false, -1);
      render(1);

      note(64, true, static_cast<int64_t>(blockFrame) - 50);
      render(1);
      const int lateOnset = onset();
      note(64, false, -1);
      render(1);
      double timed[kStatusSlotCount]{};
      bespoke_audio_read_status(timed, kStatusSlotCount);
      TEST("A frame already rendered plays at the next block start",
           lateOnset == base && timed[kStatusTimedNotes] == 2 && timed[kStatusLateNotes] == 1 &&
           timed[kStatusSplitBlocks] >= 1);
      double histogramTotal = 0.0;
      for (int i = 0; i < kAudioBlockTimeBuckets; ++i)
         histogramTotal += timed[kStatusBlockTimeHistogram + i];
      TEST("Block-time histogram counts every block", histogramTotal == timed[kStatusCallbackCount]);
      TEST("Status reports the engine's scope (Node here, 3 in the AudioWorklet) and no RT locks",
           timed[kStatusEngineScope] == static_cast<double>(AudioThreadScope::Node) && timed[kStatusRtLocks] == 0.0 &&
           timed[kStatusRtCountsAllocations] == 1.0 && timed[kStatusRtAllocations] == 0.0);

      // A Script module without Note cables plays like a keyboard, echoed to the MIDI output.
      bespoke_audio_set_midi_out_capture(1);
      int midi[16];
      (void)bespoke_audio_drain_midi_out(midi, 16);
      directNote(5, 62, true, -1);
      render(1);
      int got = bespoke_audio_drain_midi_out(midi, 16);
      TEST("Uncabled direct Script note plays like a keyboard and is echoed to MIDI out",
           onset() == base && got == 1 && (midi[0] & 0x7F) == 62 && ((midi[0] >> 20) & 1) == 1);
      bespoke_audio_apply_events(scriptClear, 1);
      render(2);
      got = bespoke_audio_drain_midi_out(midi, 16);
      TEST("ScriptClear releases what the script still holds",
           lastBlockSilent() && got == 1 && (midi[0] & 0x7F) == 62 && ((midi[0] >> 20) & 1) == 0);
      bespoke_audio_set_midi_out_capture(0);

      // With Note cables the note goes through the module's own queue, on its frame.
      TEST("Cabled test graph applies",
           bespoke_audio_apply_snapshot(cabledWire.data(), static_cast<int>(cabledWire.size()), 1) == 0);
      render(1);
      directNote(5, 60, true, static_cast<int64_t>(blockFrame) + 50);
      render(2);
      const int cabledOnset = onset();
      bespoke_audio_apply_events(scriptClear, 1);
      render(2);
      bespoke_audio_read_status(timed, kStatusSlotCount);
      TEST("Cabled direct Script note plays through the module on its frame (±1), released on clear",
           std::abs(cabledOnset - (base + 50)) <= 1 && lastBlockSilent() && timed[kStatusDirectScriptNotes] == 2);
      resetScriptNoteQueues();
   }

   store.setDeviceSampleRate(previousDeviceRate);
   audioHealthReset();
   audioHealthSetBackend(0);
}
