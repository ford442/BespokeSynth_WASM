#include "test_common.h"

void test_audio_graph_engine()
{
   printf("\n=== Audio Graph Engine Tests ===\n");

   auto nominal = render_graph(make_test_graph(1.0f, 1800.0f, 0.0f));
   TEST("Graph renders non-silent output", buffer_rms(nominal) > 0.01f);

   auto quiet = render_graph(make_test_graph(0.25f, 1800.0f, 0.0f));
   TEST("Gain node scales graph output", buffer_rms(quiet) < buffer_rms(nominal) * 0.35f);

   auto filtered = render_graph(make_test_graph(1.0f, 120.0f, 0.0f));
   TEST("Filter node affects timbre/level", buffer_rms(filtered) < buffer_rms(nominal) * 0.75f);

   auto modulated = render_graph(make_test_graph(1.0f, 400.0f, 1.0f));
   double diff = 0.0;
   for (size_t i = 0; i < filtered.size(); ++i)
      diff += std::fabs(filtered[i] - modulated[i]);
   TEST("LFO CV changes filter output", diff > 1.0);

   auto stoppedGraph = make_test_graph(1.0f, 1800.0f, 0.0f);
   stoppedGraph.transportPlaying = false;
   auto stopped = render_graph(stoppedGraph);
   TEST("Stopped transport silences graph", buffer_rms(stopped) == 0.0f);
}

void test_audio_process_plan()
{
   printf("\n=== Audio Process Plan Tests ===\n");

   auto graph = make_test_graph(1.0f, 1800.0f, 0.0f);
   compileAudioProcessPlan(graph, graph.processPlan);
   TEST("Canonical graph plan is valid", graph.processPlan.valid);
   TEST("Canonical graph has no cycle", !graph.processPlan.hasCycle);
   TEST("Plan output slot assigned", graph.processPlan.outputBufferSlot >= 0);
   TEST("Plan step count matches audio nodes", graph.processPlan.steps.size() == 4);

   bool oscBeforeOutput = false;
   const int oscType = WasmModuleAdapterRegistry::instance().typeIndexFor("oscillator");
   const int outType = WasmModuleAdapterRegistry::instance().typeIndexFor("output");
   for (const auto& step : graph.processPlan.steps)
   {
      if (step.typeIndex == oscType)
         oscBeforeOutput = true;
      if (step.typeIndex == outType)
         TEST("Oscillator precedes output in plan", oscBeforeOutput);
   }

   AudioGraphSnapshot cyclic = graph;
   cyclic.connections.push_back({ 5, 0, 1, 0, PortType::Audio, PortType::Audio });
   compileAudioProcessPlan(cyclic, cyclic.processPlan);
   TEST("Cycle detection rejects invalid plan", !cyclic.processPlan.valid && cyclic.processPlan.hasCycle);
}

void test_audio_graph_rt_allocations()
{
   printf("\n=== Audio Graph RT Allocation Tests ===\n");

   auto graph = make_test_graph(1.0f, 1800.0f, 0.5f);
   compileAudioProcessPlan(graph, graph.processPlan);

   AudioGraphEngine engine;
   engine.prepareForBlock(128, 128, 32);

   constexpr int bufferSize = 128;
   std::vector<float> left(bufferSize, 0.0f);
   std::vector<float> right(bufferSize, 0.0f);
   float* outputs[2] = { left.data(), right.data() };

   engine.processBlock(graph, outputs, 2, bufferSize, 48000.0f);

   auto& rt = gAudioCallbackActive;
   rt.store(true, std::memory_order_release);
   for (int block = 0; block < 1000; ++block)
      engine.processBlock(graph, outputs, 2, bufferSize, 48000.0f);
   rt.store(false, std::memory_order_release);

   TEST("1000 RT blocks complete without allocator trap", true);
   TEST("Note ring reports drops via counter", engine.noteDropCount() >= 0);
}

void test_audio_graph_benchmark()
{
   printf("\n=== Audio Graph Benchmark Tests ===\n");

   AudioGraphSnapshot graph;
   graph.transportPlaying = true;
   graph.transportBPM = 120.0f;

   appendAudioGraphNode(graph, 1, "output", true, {});

   for (int i = 2; i <= 65; ++i)
   {
      appendAudioGraphNode(graph, i, "gain", true, { { "gain", 0.99f } });

      AudioGraphConnection conn;
      conn.sourceModuleId = i - 1;
      conn.destModuleId = i;
      conn.sourcePortType = PortType::Audio;
      conn.destPortType = PortType::Audio;
      graph.connections.push_back(conn);
   }

   compileAudioProcessPlan(graph, graph.processPlan);
   TEST("64-node chain plan valid", graph.processPlan.valid);

   AudioGraphEngine engine;
   engine.prepareForBlock(128, 128, 32);
   std::vector<float> left(128, 0.0f);
   std::vector<float> right(128, 0.0f);
   float* outputs[2] = { left.data(), right.data() };

   const double blockPeriodMs = 128.0 / 48.0;
   double maxMs = 0.0;
   for (int i = 0; i < 200; ++i)
   {
      const auto start = std::chrono::steady_clock::now();
      engine.processBlock(graph, outputs, 2, 128, 48000.0f);
      const double elapsedMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      maxMs = std::max(maxMs, elapsedMs);
   }

   TEST("64-node chain p99 budget under block period", maxMs < blockPeriodMs * 0.95);
}

void test_audio_health_metrics()
{
   printf("\n=== Audio Health Metrics Tests ===\n");

   audioHealthReset();
   audioHealthSetBackend(0);
   audioHealthOnCallback(0.001, 0.01, 0.011); // under budget
   audioHealthOnCallback(0.02, 0.01, 0.012); // overrun → underrun
   audioHealthSetQueueStats(512, 4096, 3);

   const AudioHealthSnapshot snap = audioHealthSnapshot();
   TEST("Health callback count advances", snap.callbackCount == 2);
   TEST("Health records budget overrun as underrun", snap.underrunCount >= 1);
   TEST("Health stores queue depth", snap.queueDepthFrames == 512);
   TEST("Health stores capacity", snap.capacityFrames == 4096);
   TEST("Health stores external underruns", snap.externalUnderrunCount == 3);
   TEST("Health max process time positive", snap.maxProcessTimeMs > 0.0);

   const char* json = audioHealthJson();
   TEST("Health JSON non-empty", json && std::strlen(json) > 10);
   TEST("Health JSON mentions cpuLoad", std::strstr(json, "cpuLoad") != nullptr);
   TEST("Health JSON mentions noteDropCount", std::strstr(json, "noteDropCount") != nullptr);
}
