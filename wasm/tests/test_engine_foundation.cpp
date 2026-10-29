// Engine foundation (#125): the control fast path, live-param adoption rules, port channel
// declarations (panner), stereo effects (reverb decorrelation, delay ping-pong) and the Output DC blocker.
#include "test_common.h"
#include "BespokeWasm/AudioGraphSnapshotCodec.h"
#include "BespokeWasm/adapters/OutputModuleAdapter.h"
#include "BespokeWasm/adapters/PannerModuleAdapter.h"
#include "BespokeWasm/adapters/SamplerModuleAdapter.h"
#include "BespokeWasm/BundledKit.h"
#include "BespokeWasm/LooperArena.h"
#include "BespokeWasm/ResourceLifetime.h"

namespace
{
   constexpr int kBlock = 512;
   constexpr float kRate = 44100.0f;

   struct BlockPeaks
   {
      float left = 0.0f;
      float right = 0.0f;
   };

   BlockPeaks renderPeaks(AudioGraphEngine& engine, const AudioGraphSnapshot& graph, int blocks)
   {
      std::vector<float> left(kBlock), right(kBlock);
      float* outputs[2] = { left.data(), right.data() };
      BlockPeaks peaks;
      for (int block = 0; block < blocks; ++block)
      {
         engine.processBlock(graph, outputs, 2, kBlock, kRate);
         peaks = {};
         for (int i = 0; i < kBlock; ++i)
         {
            peaks.left = std::max(peaks.left, std::fabs(left[static_cast<size_t>(i)]));
            peaks.right = std::max(peaks.right, std::fabs(right[static_cast<size_t>(i)]));
         }
      }
      return peaks; // of the last block
   }

   /** A canvas wired like the runtime's local mode: publishes prepare the engine, controls queue. */
   struct LiveRig
   {
      ModuleCanvas canvas;
      AudioGraphEngine engine;
      bool sinkAccepts = true;

      LiveRig()
      {
         engine.prepareForBlock(kBlock, 128, 32);
         canvas.setSnapshotListener([this](const AudioGraphSnapshot& graph)
                                    {
                                       engine.prepareRuntimeStates(graph);
                                    });
         canvas.setParamSink([this](const ParamChange& change)
                             {
                                return sinkAccepts && engine.queueParamChange(change);
                             });
      }

      BlockPeaks render(int blocks)
      {
         const auto graph = canvas.getAudioGraphSnapshot();
         return renderPeaks(engine, *graph, blocks);
      }
   };

   /** oscillator → gain → output (limiter off); returns the gain module id. */
   int buildGainChain(ModuleCanvas& canvas)
   {
      const int osc = canvas.createModule("oscillator", 0, 0);
      const int gain = canvas.createModule("gain", 200, 0);
      const int output = canvas.createModule("output", 400, 0);
      canvas.setModuleControlValue(osc, "frequency", 220.0f);
      canvas.setModuleControlValue(osc, "volume", 0.5f);
      canvas.setModuleControlValue(output, "limit", 0.0f);
      canvas.connectModules(osc, 0, gain, 0);
      canvas.connectModules(gain, 0, output, 0);
      return gain;
   }

   double correlation(const std::vector<float>& a, const std::vector<float>& b)
   {
      double ab = 0.0, aa = 0.0, bb = 0.0;
      for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
      {
         ab += static_cast<double>(a[i]) * b[i];
         aa += static_cast<double>(a[i]) * a[i];
         bb += static_cast<double>(b[i]) * b[i];
      }
      return aa > 0.0 && bb > 0.0 ? ab / std::sqrt(aa * bb) : 1.0;
   }

   void testFastPath()
   {
      LiveRig rig;
      const int gain = buildGainChain(rig.canvas);
      // Unrecorded, so the undo below cannot coalesce with it.
      rig.canvas.setModuleControlValue(gain, "gain", 1.0f, false);
      rig.canvas.setTransportPlaying(true);
      const BlockPeaks loud = rig.render(8);

      const uint64_t compilesBefore = audioPlanCompileCount();
      const auto canvasBefore = rig.canvas.stats();
      const auto engineBefore = rig.engine.stats();
      // 10 s of a 60 Hz knob drag, one 512-frame block per frame (close to real time at 44.1 kHz).
      for (int frame = 0; frame < 600; ++frame)
      {
         const float value = 1.0f - 0.9f * static_cast<float>(frame) / 599.0f;
         rig.canvas.setModuleControlValue(gain, "gain", value, false);
         rig.render(1);
      }
      const BlockPeaks quiet = rig.render(8);
      const auto canvasAfter = rig.canvas.stats();
      const auto engineAfter = rig.engine.stats();

      TEST("Knob drag compiles no process plan", audioPlanCompileCount() == compilesBefore);
      TEST("Knob drag publishes no snapshot", canvasAfter.publishCount == canvasBefore.publishCount);
      TEST("Knob drag never calls prepareRuntimeStates", engineAfter.prepareCount == engineBefore.prepareCount);
      // The first frame sets the value the knob already has, which sends nothing.
      TEST("Knob drag reaches the engine as param changes",
           canvasAfter.paramChangesSent - canvasBefore.paramChangesSent == 599 &&
           engineAfter.paramChangesApplied - engineBefore.paramChangesApplied == 599);
      TEST("The dragged gain is what plays", loud.left > 0.3f && std::fabs(quiet.left / loud.left - 0.1f) < 0.01f);
      TEST("No node was skipped for lack of state", engineAfter.skippedNodes == 0);

      // Undo / redo of a control sync params instead of publishing.
      rig.canvas.setModuleControlValue(gain, "gain", 0.5f);
      rig.render(8);
      const auto beforeUndo = rig.canvas.stats();
      rig.canvas.undo();
      const BlockPeaks undone = rig.render(8);
      rig.canvas.redo();
      const BlockPeaks redone = rig.render(8);
      TEST("Undo / redo of a control publishes nothing", rig.canvas.stats().publishCount == beforeUndo.publishCount);
      TEST("Undo / redo of a control is heard",
           std::fabs(undone.left / loud.left - 0.1f) < 0.01f && std::fabs(redone.left / loud.left - 0.5f) < 0.01f);

      // Transport play / stop and tempo are global changes.
      const auto beforeStop = rig.canvas.stats();
      rig.canvas.setTransportPlaying(false);
      const BlockPeaks stopped = rig.render(2);
      rig.canvas.setTransportBPM(97.0f);
      rig.canvas.setTransportPlaying(true);
      const BlockPeaks playing = rig.render(4);
      TEST("Transport edits publish nothing", rig.canvas.stats().publishCount == beforeStop.publishCount);
      TEST("Stop through the fast path mutes, play resumes", stopped.left == 0.0f && playing.left > 0.1f);

      // A full queue falls back to a publish, which still carries the value.
      rig.sinkAccepts = false;
      const auto beforeFull = rig.canvas.stats();
      rig.canvas.setModuleControlValue(gain, "gain", 1.0f, false);
      const BlockPeaks fallback = rig.render(8);
      TEST("A refused change republishes", rig.canvas.stats().publishCount == beforeFull.publishCount + 1);
      TEST("The republished value plays", std::fabs(fallback.left / loud.left - 1.0f) < 0.01f);
      rig.sinkAccepts = true;

      // The live copy for the worklet carries every change since the publish.
      rig.canvas.setModuleControlValue(gain, "gain", 0.25f, false);
      const AudioGraphSnapshot live = rig.canvas.liveAudioGraphSnapshot();
      const auto published = rig.canvas.getAudioGraphSnapshot();
      bool liveHasGain = false;
      for (const auto& node : live.nodes)
      {
         if (node.id == gain)
            liveHasGain = static_cast<const GainParams*>(live.paramsFor(node))->gain == 0.25f;
      }
      TEST("liveAudioGraphSnapshot carries fast-path values and seq",
           liveHasGain && live.generation == published->generation && live.paramSeq == rig.canvas.stats().paramSeq);
      std::vector<uint8_t> wire;
      encodeAudioGraphSnapshot(live, wire);
      AudioGraphSnapshot decoded;
      std::string error;
      TEST("The wire format carries generation and paramSeq",
           decodeAudioGraphSnapshot(wire.data(), wire.size(), decoded, error) && decoded.generation == live.generation &&
           decoded.paramSeq == live.paramSeq);
   }

   /** Engine-side ordering: a snapshot built before a change must not undo it, and stale changes are dropped. */
   void testAdoptionOrder()
   {
      ModuleCanvas canvas;
      const int gain = buildGainChain(canvas);
      canvas.setModuleControlValue(gain, "gain", 1.0f);
      canvas.setTransportPlaying(true);
      AudioGraphSnapshot built = canvas.createAudioGraphSnapshot();
      const GainModuleAdapter gainAdapter;
      const ParamId gainId = findParam(gainAdapter.paramTable(), "gain")->id;

      AudioGraphEngine engine;
      engine.prepareForBlock(kBlock, 128, 32);
      AudioGraphSnapshot first = built;
      first.generation = 1;
      first.paramSeq = 10;
      engine.prepareRuntimeStates(first);
      const BlockPeaks loud = renderPeaks(engine, first, 4);

      // seq 11 arrives (ring) before the snapshot that was built at seq 10 (postMessage).
      TEST("Param queue accepts a change", engine.queueParamChange({ gain, gainId, 0.2f, 11 }));
      renderPeaks(engine, first, 8);
      AudioGraphSnapshot second = built;
      second.generation = 2;
      second.paramSeq = 10;
      engine.prepareRuntimeStates(second);
      const BlockPeaks afterAdopt = renderPeaks(engine, second, 2);
      TEST("A newer change survives adopting an older snapshot", std::fabs(afterAdopt.left / loud.left - 0.2f) < 0.01f);

      AudioGraphSnapshot third = built;
      third.generation = 3;
      third.paramSeq = 20; // reflects gain 1.0 as of seq 20
      engine.prepareRuntimeStates(third);
      renderPeaks(engine, third, 1);
      const uint64_t staleBefore = engine.stats().paramChangesStale;
      TEST("Param queue accepts a late change", engine.queueParamChange({ gain, gainId, 0.6f, 15 }));
      const BlockPeaks afterStale = renderPeaks(engine, third, 4);
      TEST("A change older than the adopted snapshot is skipped",
           engine.stats().paramChangesStale == staleBefore + 1 && std::fabs(afterStale.left / loud.left - 1.0f) < 0.01f);

      // Smoothing: a jump in gain glides over a few blocks instead of stepping.
      TEST("Param queue accepts a jump", engine.queueParamChange({ gain, gainId, 0.0f, 21 }));
      const BlockPeaks gliding = renderPeaks(engine, third, 1);
      const BlockPeaks settled = renderPeaks(engine, third, 40);
      TEST("Gain changes glide (one block after a jump to 0 is still sounding)",
           gliding.left > 0.05f * loud.left && settled.left < 0.01f * loud.left);
   }

   void testPanner()
   {
      auto pannedPeaks = [](float pan, bool stereoSource, int& planMix)
      {
         ModuleCanvas canvas;
         const int osc = canvas.createModule("oscillator", 0, 0);
         const int panner = canvas.createModule("panner", 200, 0);
         const int output = canvas.createModule("output", 400, 0);
         canvas.setModuleControlValue(osc, "volume", 0.3f);
         canvas.setModuleControlValue(output, "limit", 0.0f);
         canvas.setModuleControlValue(panner, "pan", pan);
         int source = osc;
         if (stereoSource)
         {
            // A mixer panned hard left makes a stereo cable whose right side is silent.
            source = canvas.createModule("mixer", 100, 0);
            canvas.setModuleControlValue(source, "pan1", 0.0f);
            canvas.setModuleControlValue(source, "level1", 1.0f);
            canvas.setModuleControlValue(source, "master", 1.0f);
            canvas.connectModules(osc, 0, source, 0);
         }
         canvas.connectModules(source, 0, panner, 0);
         canvas.connectModules(panner, 0, output, 0);
         canvas.setTransportPlaying(true);
         AudioGraphSnapshot graph = canvas.createAudioGraphSnapshot();
         planMix = -1;
         for (const auto& step : graph.processPlan.steps)
         {
            if (step.moduleId == panner && step.audioInputs.count == 1)
               planMix = static_cast<int>(graph.processPlan.audioInputMix[step.audioInputs.begin]);
         }
         AudioGraphEngine engine;
         engine.prepareForBlock(kBlock, 128, 32);
         engine.prepareRuntimeStates(graph);
         return renderPeaks(engine, graph, 4);
      };
      int mix = -1;
      const BlockPeaks left = pannedPeaks(0.0f, false, mix);
      const BlockPeaks right = pannedPeaks(1.0f, false, mix);
      const BlockPeaks centre = pannedPeaks(0.5f, false, mix);
      TEST("Panner hard left: only L sounds", left.left > 0.1f && left.right < 1e-6f);
      TEST("Panner hard right: only R sounds", right.right > 0.1f && right.left < 1e-6f);
      TEST("Panner centre: equal sides at unity", std::fabs(centre.left - centre.right) < 1e-4f &&
                                                  std::fabs(left.left / centre.left - std::sqrt(2.0f)) < 0.01f);
      const BlockPeaks folded = pannedPeaks(1.0f, true, mix);
      TEST("A stereo cable into the Mono port is folded by the plan",
           mix == static_cast<int>(PlanChannelMix::ToMono) && folded.right > 0.05f && folded.left < 1e-6f);
   }

   void testDelayPingPong()
   {
      DelayModuleAdapter adapter;
      DelayParams params{};
      adapter.fillParams({ { "time", 0.01f }, { "feedback", 0.5f }, { "mix", 1.0f }, { "pingpong", 1.0f } }, &params);
      std::vector<uint8_t> storage(adapter.runtimeStateSize());
      adapter.initRuntimeState(storage.data());

      WasmAudioProcessContext context{};
      context.sampleRate = kRate;
      context.numSamples = 1024;
      context.inputChannels = 1;
      TEST("Delay goes stereo when ping-pong is on", adapter.outputChannelCount(&params, context) == 2);
      DelayParams plain{};
      adapter.fillParams({}, &plain);
      TEST("Delay stays mono for a mono input without ping-pong", adapter.outputChannelCount(&plain, context) == 1);

      // An impulse on the left: echoes alternate L (441), R (882), L (1323).
      context.numChannels = 2;
      std::vector<float> left(1024, 0.0f), right(1024, 0.0f);
      left[0] = 1.0f;
      float* channels[2] = { left.data(), right.data() };
      adapter.processAudio(storage.data(), &params, channels, context);
      auto near = [](float value, float want)
      {
         return std::fabs(value - want) < 1e-3f;
      };
      TEST("Ping-pong first echo is on the left only", near(left[441], 0.5f) && near(right[441], 0.0f));
      TEST("Ping-pong second echo is on the right only", near(right[882], 0.25f) && near(left[882], 0.0f));
      adapter.destroyRuntimeState(storage.data());
   }

   void testOutputDcBlocker()
   {
      OutputModuleAdapter adapter;
      auto settled = [&adapter](float dcBlock)
      {
         OutputParams params{};
         adapter.fillParams({ { "limit", 0.0f }, { "dcblock", dcBlock } }, &params);
         std::vector<uint8_t> storage(adapter.runtimeStateSize());
         adapter.initRuntimeState(storage.data());
         WasmAudioProcessContext context{};
         context.sampleRate = kRate;
         context.numSamples = kBlock;
         context.numChannels = 2;
         std::vector<float> left(kBlock), right(kBlock);
         float* channels[2] = { left.data(), right.data() };
         for (int block = 0; block < static_cast<int>(kRate) / kBlock; ++block) // ~1 s of +0.5 DC
         {
            std::fill(left.begin(), left.end(), 0.5f);
            std::fill(right.begin(), right.end(), -0.5f);
            adapter.processAudio(storage.data(), &params, channels, context);
         }
         adapter.destroyRuntimeState(storage.data());
         return std::max(std::fabs(left.back()), std::fabs(right.back()));
      };
      TEST("Output DC blocker removes a constant offset", settled(1.0f) < 1e-3f);
      TEST("Output DC blocker off passes the offset (v4 patches)", std::fabs(settled(0.0f) - 0.5f) < 1e-6f);
   }

   const SampleBuffer* boundSample(const AudioGraphSnapshot& graph, int moduleId)
   {
      for (const auto& node : graph.nodes)
      {
         if (node.id == moduleId)
            return static_cast<const SamplerParams*>(graph.paramsFor(node))->sample;
      }
      return nullptr;
   }

   void testResourceLifetime()
   {
      const size_t savedBudget = resourceBudget();
      setResourceBudget(0); // evict everything unused, as soon as it is safe
      const std::string snareHash = bundledKitSamples()[1].sha256;

      // Wired like the bridge: each publish prepares the engine, then reclaims.
      ModuleCanvas canvas;
      AudioGraphEngine engine;
      engine.prepareForBlock(kBlock, 128, 32);
      uint64_t published = 0;
      canvas.setSnapshotListener([&](const AudioGraphSnapshot& graph)
                                 {
                                    engine.prepareRuntimeStates(graph);
                                    published = graph.generation;
                                 });
      canvas.republishAudioGraph();
      collectUnusedResources(published); // whatever earlier tests left unreferenced
      const size_t baseline = residentResourceBytes();

      const int sampler = canvas.createModule("sampler", 0, 0);
      canvas.setModuleStringProperty(sampler, "sampleHash", snareHash);
      const int looper = canvas.createModule("looper", 200, 0);
      collectUnusedResources(published);
      const size_t loaded = residentResourceBytes();
      TEST("Referenced samples and looper arenas stay resident at budget 0",
           loaded > baseline + LooperArenaPool::kArenaBytes &&
           boundSample(*canvas.getAudioGraphSnapshot(), sampler) != nullptr);

      canvas.deleteModule(sampler);
      const uint64_t deletedAt = published;
      collectUnusedResources(deletedAt - 1);
      TEST("A sample outlives every snapshot the engine may still process", residentResourceBytes() == loaded);
      collectUnusedResources(deletedAt);
      const size_t afterDelete = residentResourceBytes();
      TEST("Deleting the sampler frees its sample once safe",
           afterDelete == baseline + LooperArenaPool::kArenaBytes &&
           SampleStore::instance().findByHash(snareHash) == nullptr);

      bool restored = true;
      for (int cycle = 0; cycle < 50; ++cycle)
      {
         canvas.undo();
         collectUnusedResources(published);
         const SampleBuffer* sample = boundSample(*canvas.getAudioGraphSnapshot(), sampler);
         restored = restored && sample && sample->hash() == snareHash;
         canvas.redo();
         collectUnusedResources(published);
      }
      TEST("Undo reloads the evicted sample by hash, 50 times", restored);
      TEST("After 50 undo / redo cycles memory is back to baseline", residentResourceBytes() == afterDelete);

      canvas.deleteModule(looper);
      collectUnusedResources(published);
      TEST("Deleting the looper frees its arena", residentResourceBytes() == baseline &&
                                                  LooperArenaPool::instance().find(looper) == nullptr);
      canvas.undo();
      TEST("Undo of the looper delete gets an arena again", LooperArenaPool::instance().find(looper) != nullptr);

      setResourceBudget(savedBudget);
   }

   void testReverbDecorrelation()
   {
      ModuleCanvas canvas;
      const int noise = canvas.createModule("noise", 0, 0);
      const int reverb = canvas.createModule("reverb", 200, 0);
      const int output = canvas.createModule("output", 400, 0);
      canvas.setModuleControlValue(noise, "volume", 0.2f);
      canvas.setModuleControlValue(reverb, "mix", 1.0f);
      canvas.setModuleControlValue(output, "limit", 0.0f);
      canvas.connectModules(noise, 0, reverb, 0);
      canvas.connectModules(reverb, 0, output, 0);
      canvas.setTransportPlaying(true);
      AudioGraphSnapshot graph = canvas.createAudioGraphSnapshot();
      AudioGraphEngine engine;
      engine.prepareForBlock(kBlock, 128, 32);
      engine.prepareRuntimeStates(graph);
      std::vector<float> left(kBlock), right(kBlock), allL, allR;
      float* outputs[2] = { left.data(), right.data() };
      for (int block = 0; block < 40; ++block)
      {
         engine.processBlock(graph, outputs, 2, kBlock, kRate);
         if (block >= 10)
         {
            allL.insert(allL.end(), left.begin(), left.end());
            allR.insert(allR.end(), right.begin(), right.end());
         }
      }
      const double corr = correlation(allL, allR);
      printf("  reverb L/R correlation (mono noise in): %.3f\n", corr);
      TEST("Reverb outputs decorrelated L/R from a mono input", buffer_rms(allL) > 0.001 && corr < 0.9);
   }
}

void test_engine_foundation()
{
   printf("\n=== Engine Foundation (#125) ===\n");
   testFastPath();
   testAdoptionOrder();
   testPanner();
   testReverbDecorrelation();
   testDelayPingPong();
   testOutputDcBlocker();
   testResourceLifetime();
}
