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
int bespoke_audio_adopt_sample(int id, int channels, int frames, int sampleRate, const float* planar);
void bespoke_audio_apply_events(const int* words, int count);
void bespoke_audio_process(float* left, float* right, int frames);
void bespoke_audio_read_status(double* out, int count);
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
      size_t pos = 4 + 4 + 1 + sizeof(float) + sizeof(GraphMusicScale);
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
      pos += 3 + 4 * 4; // plan flags and scalars
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
      broken.processPlan.steps[0].inputBufferSlots.count = 99;
      encodeAudioGraphSnapshot(broken, brokenWire);
      const bool rejectedCount = !decodeAudioGraphSnapshot(brokenWire.data(), brokenWire.size(), ignored, error);
      TEST("Out-of-range plan indices and SmallVec counts are rejected",
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

   // Sampler params name their sample by id; the receiver resolves the id in its own store.
   {
      const std::vector<float> other = sinePlanar(2205, 220.0f, 0.25f);
      store.adopt(sampleId, std::make_shared<SampleBuffer>(other, 1, 44100, "codec-test-b", "b.wav"));
      AudioGraphSnapshot rebound;
      const bool ok = decodeAudioGraphSnapshot(wire.data(), wire.size(), rebound, error);
      const auto* sampler = ok ? nodeById(rebound, 6) : nullptr;
      const auto* params = sampler ? static_cast<const SamplerParams*>(rebound.paramsFor(*sampler)) : nullptr;
      TEST("Sampler params rebind by sample id, not the sender's pointer",
           params && params->sampleId == sampleId && params->sample == store.findById(sampleId) &&
           params->sample->hash() == "codec-test-b");
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
   bespoke_audio_process(left.data(), right.data(), kQuantum);
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

   TEST("Sample copy is adopted under the sender's id",
        bespoke_audio_adopt_sample(sampleId, 1, 4410, 44100, sine.data()) == 0 && store.findById(sampleId) &&
        store.findById(sampleId)->hash().empty());
   TEST("Snapshot applies", bespoke_audio_apply_snapshot(wire.data(), static_cast<int>(wire.size()), 0) == 0);

   bespoke_audio_process(left.data(), right.data(), kQuantum);
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
      bespoke_audio_process(left.data(), right.data(), kQuantum);
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
   bespoke_audio_process(bigLeft.data(), bigRight.data(), 4096);
   TEST("A block larger than the init size renders silence instead of overrunning",
        peakOf(bigLeft) == 0.0f && peakOf(bigRight) == 0.0f);

   store.setDeviceSampleRate(previousDeviceRate);
   audioHealthReset();
   audioHealthSetBackend(0);
}
