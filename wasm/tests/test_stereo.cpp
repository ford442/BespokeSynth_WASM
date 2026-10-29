// --- Stereo graph: planar arena, mixer pan, stereo samples, output limiter ---
#include "test_common.h"
#include "BespokeWasm/adapters/OutputModuleAdapter.h"

namespace
{
   struct StereoRender
   {
      std::vector<float> left;
      std::vector<float> right;
   };

   AudioGraphConnection audioCable(int src, int dst, int dstPort = 0)
   {
      AudioGraphConnection conn;
      conn.sourceModuleId = src;
      conn.destModuleId = dst;
      conn.destPortIndex = dstPort;
      return conn;
   }

   StereoRender renderStereo(AudioGraphSnapshot& graph, AudioGraphEngine& engine, int blocks, int blockSize = 512,
                             int noteOnPitch = -1)
   {
      compileAudioProcessPlan(graph, graph.processPlan);
      engine.prepareForBlock(blockSize, 32, 8);
      engine.prepareRuntimeStates(graph);
      StereoRender out;
      std::vector<float> left(static_cast<size_t>(blockSize));
      std::vector<float> right(static_cast<size_t>(blockSize));
      for (int b = 0; b < blocks; ++b)
      {
         if (b == 0 && noteOnPitch >= 0)
         {
            AudioGraphEngine::NoteMessage note;
            note.pitch = noteOnPitch;
            note.velocity = 1.0f;
            note.isNoteOn = true;
            engine.queueNote(note);
         }
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

   /** Stereo WAV with a sine on L and silence on R, loaded into the SampleStore. */
   const SampleBuffer* loadLeftOnlyStereoSample()
   {
      constexpr int frames = 4410;
      std::vector<float> interleaved(static_cast<size_t>(frames) * 2u, 0.0f);
      for (int i = 0; i < frames; ++i)
         interleaved[static_cast<size_t>(i) * 2u] = 0.5f * std::sin(6.28318530718f * 440.0f * i / 44100.0f);
      std::vector<uint8_t> wav;
      std::string error;
      if (!encodeWav(interleaved.data(), frames, 2, 44100, 32, wav, error))
         return nullptr;
      const int id = SampleStore::instance().loadFromMemory(wav.data(), static_cast<int>(wav.size()), "lr.wav", 44100);
      return SampleStore::instance().findById(id);
   }
}

void test_stereo_dsp_kernels()
{
   printf("\n=== Stereo DSP Kernel Tests ===\n");

   constexpr int n = 37; // vector body + scalar tail
   std::vector<float> src(n), l(n, 0.25f), r(n, -0.5f), refL(l), refR(r);
   for (int i = 0; i < n; ++i)
      src[static_cast<size_t>(i)] = std::sin(0.37f * i) * 1.5f;

   dsp::accumulatePanned(l.data(), r.data(), src.data(), n, 0.8f, 0.3f);
   bool pannedOk = true;
   for (int i = 0; i < n; ++i)
   {
      refL[static_cast<size_t>(i)] += src[static_cast<size_t>(i)] * 0.8f;
      refR[static_cast<size_t>(i)] += src[static_cast<size_t>(i)] * 0.3f;
      pannedOk = pannedOk && std::fabs(l[static_cast<size_t>(i)] - refL[static_cast<size_t>(i)]) < 1e-6f &&
                 std::fabs(r[static_cast<size_t>(i)] - refR[static_cast<size_t>(i)]) < 1e-6f;
   }
   TEST("accumulatePanned matches scalar reference", pannedOk);

   std::vector<float> mid(n);
   dsp::midDownmix(mid.data(), l.data(), r.data(), n);
   bool midOk = true;
   for (int i = 0; i < n; ++i)
      midOk = midOk && std::fabs(mid[static_cast<size_t>(i)] - (l[static_cast<size_t>(i)] + r[static_cast<size_t>(i)]) * 0.5f) < 1e-6f;
   TEST("midDownmix matches scalar reference", midOk);

   float refPeak = 0.0f;
   for (float v : src)
      refPeak = std::max(refPeak, std::fabs(v));
   TEST("peakAbs matches scalar reference", std::fabs(dsp::peakAbs(src.data(), n) - refPeak) < 1e-7f);
   src[35] = -4.0f; // tail lane
   TEST("peakAbs sees the scalar tail", dsp::peakAbs(src.data(), n) == 4.0f);
   src[2] = std::nanf("");
   TEST("peakAbs ignores NaN", dsp::peakAbs(src.data(), n) == 4.0f);
   TEST("peakAbs of empty span is 0", dsp::peakAbs(src.data(), 0) == 0.0f);
}

void test_stereo_graph()
{
   printf("\n=== Stereo Graph Tests ===\n");

   // Mono chain stays mono: L and R are bit-identical.
   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      appendAudioGraphNode(graph, 1, "oscillator", true, { { "frequency", 220.0f }, { "volume", 0.5f } });
      appendAudioGraphNode(graph, 2, "filter", true, { { "cutoff", 1200.0f } });
      appendAudioGraphNode(graph, 3, "output", true, {});
      graph.connections = { audioCable(1, 2), audioCable(2, 3) };
      AudioGraphEngine engine;
      auto out = renderStereo(graph, engine, 4);
      TEST("Mono chain has signal", buffer_rms(out.left) > 0.05f);
      TEST("Mono chain upmixes identically to L and R", out.left == out.right);
   }

   // Mixer pan: hard left silences R; centre keeps unity on both sides.
   auto mixerGraph = [](float pan, float limit, float volume = 0.5f)
   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      appendAudioGraphNode(graph, 1, "oscillator", true, { { "frequency", 220.0f }, { "volume", volume }, { "waveform", 0.0f } });
      appendAudioGraphNode(graph, 2, "mixer", true, { { "level1", 1.0f }, { "pan1", pan } });
      appendAudioGraphNode(graph, 3, "output", true, { { "limit", limit } });
      graph.connections = { audioCable(1, 2, 0), audioCable(2, 3) };
      return graph;
   };
   {
      AudioGraphSnapshot hardLeft = mixerGraph(0.0f, 1.0f);
      AudioGraphEngine engine;
      auto out = renderStereo(hardLeft, engine, 4);
      TEST("Mixer pan hard-left keeps L", buffer_rms(out.left) > 0.1f);
      TEST("Mixer pan hard-left silences R", peakOf(out.right) < 1e-6f);

      AudioGraphSnapshot hardRight = mixerGraph(1.0f, 1.0f);
      AudioGraphEngine engineR;
      auto outR = renderStereo(hardRight, engineR, 4);
      TEST("Mixer pan hard-right silences L", peakOf(outR.left) < 1e-4f && buffer_rms(outR.right) > 0.1f);

      AudioGraphSnapshot centre = mixerGraph(0.5f, 1.0f);
      AudioGraphSnapshot mono;
      mono.transportPlaying = true;
      appendAudioGraphNode(mono, 1, "oscillator", true, { { "frequency", 220.0f }, { "volume", 0.5f }, { "waveform", 0.0f } });
      appendAudioGraphNode(mono, 3, "output", true, {});
      mono.connections = { audioCable(1, 3) };
      AudioGraphEngine engineC;
      AudioGraphEngine engineM;
      auto outC = renderStereo(centre, engineC, 4);
      auto outM = renderStereo(mono, engineM, 4);
      TEST("Mixer centre pan is balanced", std::fabs(buffer_rms(outC.left) - buffer_rms(outC.right)) < 1e-4f);
      TEST("Mixer centre pan is unity (matches the unpanned path)",
           std::fabs(buffer_rms(outC.left) - buffer_rms(outM.left)) < 1e-3f);
   }

   // Output limiter: hard-left equal-power pan is +3 dB, so a 0.8 sine peaks near 1.13 pre-limit.
   {
      AudioGraphSnapshot limited = mixerGraph(0.0f, 1.0f, 0.8f);
      AudioGraphEngine engine;
      auto out = renderStereo(limited, engine, 8);
      const float prePeak = engine.takeOutputPeak(0);
      TEST("Output meter reports the pre-limit peak", prePeak > 1.05f);
      TEST("Output meter resets on read", engine.takeOutputPeak(0) == 0.0f);
      TEST("Output limiter keeps peaks at the ceiling",
           peakOf(out.left) <= OutputModuleAdapter::kLimiterCeiling + 1e-6f);
      TEST("Output limiter output is finite", all_finite(out.left) && all_finite(out.right));

      AudioGraphSnapshot unlimited = mixerGraph(0.0f, 0.0f, 0.8f);
      AudioGraphEngine engineOff;
      auto outOff = renderStereo(unlimited, engineOff, 8);
      TEST("Limit off falls back to the hard clip", peakOf(outOff.left) == 1.0f);
   }

   // Limiter adapter directly: a 2.0 peak goes below 1.0; NaN never reaches the device.
   {
      OutputModuleAdapter output;
      OutputParams params;
      output.fillParams({}, &params);
      TEST("Output limit defaults on", params.limit);
      OutputRuntimeState state;
      output.initRuntimeState(&state);
      std::vector<float> l(1024), r(1024);
      for (int i = 0; i < 1024; ++i)
      {
         l[static_cast<size_t>(i)] = 2.0f * std::sin(0.05f * i);
         r[static_cast<size_t>(i)] = -2.0f * std::sin(0.05f * i);
      }
      l[100] = std::nanf("");
      float* channels[2] = { l.data(), r.data() };
      WasmAudioProcessContext context;
      context.sampleRate = 44100.0f;
      context.numSamples = 1024;
      context.numChannels = 2;
      output.processAudio(&state, &params, channels, context);
      TEST("Limiter reduces a 2.0 peak below 1.0", peakOf(l) < 1.0f && peakOf(r) < 1.0f);
      TEST("Limiter output has no NaNs", all_finite(l) && all_finite(r));
      TEST("Limiter gain stays finite after a NaN", std::isfinite(state.gain) && state.gain > 0.0f);
   }

   // Stereo sample stays stereo through sampler → delay → mixer → output.
   {
      const SampleBuffer* sample = loadLeftOnlyStereoSample();
      TEST("Stereo WAV loads as 2 channels", sample && sample->channelCount() == 2);
      TEST("Stereo WAV keeps R silent", sample && dsp::peakAbs(sample->channelData(1), sample->frameCount()) == 0.0f);
      TEST("Stereo WAV keeps L energy", sample && dsp::peakAbs(sample->channelData(0), sample->frameCount()) > 0.4f);
      if (sample)
      {
         AudioGraphSnapshot graph;
         graph.transportPlaying = true;
         appendAudioGraphNode(graph, 1, "sampler", true, { { "volume", 1.0f }, { "mode", 0.0f } },
                              { { "sampleHash", sample->hash() } });
         appendAudioGraphNode(graph, 2, "delay", true, { { "mix", 0.3f } });
         appendAudioGraphNode(graph, 3, "mixer", true, { { "level1", 1.0f } });
         appendAudioGraphNode(graph, 4, "output", true, {});
         graph.connections = { audioCable(1, 2), audioCable(2, 3, 0), audioCable(3, 4) };
         AudioGraphEngine engine;
         auto out = renderStereo(graph, engine, 4, 512, 60);
         TEST("Stereo sample plays on L", buffer_rms(out.left) > 0.05f);
         TEST("Stereo sample does not collapse onto R", peakOf(out.right) < 1e-6f);
      }
   }

   // Reverb and chorus widen a mono source: L and R decorrelate.
   {
      AudioGraphSnapshot graph;
      graph.transportPlaying = true;
      appendAudioGraphNode(graph, 1, "noise", true, { { "volume", 0.3f } });
      appendAudioGraphNode(graph, 2, "chorus", true, { { "mix", 0.5f } });
      appendAudioGraphNode(graph, 3, "reverb", true, { { "mix", 0.5f } });
      appendAudioGraphNode(graph, 4, "compressor", true, {});
      appendAudioGraphNode(graph, 5, "output", true, {});
      graph.connections = { audioCable(1, 2), audioCable(2, 3), audioCable(3, 4, 0), audioCable(4, 5) };
      AudioGraphEngine engine;
      auto out = renderStereo(graph, engine, 8);
      double diff = 0.0;
      for (size_t i = 0; i < out.left.size(); ++i)
         diff += std::fabs(out.left[i] - out.right[i]);
      TEST("Chorus/reverb produce a stereo image", diff > 1.0);
      TEST("Stereo FX chain is finite", all_finite(out.left) && all_finite(out.right));

      // Same graph under the RT allocation guard: stereo arenas are pre-sized on the UI thread.
      std::vector<float> left(512), right(512);
      float* outputs[2] = { left.data(), right.data() };
      gAudioCallbackActive.store(true, std::memory_order_release);
      for (int b = 0; b < 200; ++b)
         engine.processBlock(graph, outputs, 2, 512, 44100.0f);
      gAudioCallbackActive.store(false, std::memory_order_release);
      TEST("200 stereo RT blocks complete without allocator trap", true);
   }

   // Chorus at width 0 on a mono input stays mono (no stereo cost).
   {
      ChorusModuleAdapter chorus;
      ChorusParams params;
      chorus.fillParams({ { "width", 0.0f } }, &params);
      WasmAudioProcessContext context;
      context.inputChannels = 1;
      TEST("Chorus width 0 on mono input stays mono", chorus.outputChannelCount(&params, context) == 1);
      chorus.fillParams({}, &params);
      TEST("Chorus defaults to full width", params.width == 1.0f && chorus.outputChannelCount(&params, context) == 2);
   }
}

void test_stereo_patch_migration()
{
   printf("\n=== Stereo Patch Migration Tests ===\n");

   const char* v4Json =
   "{\n"
   "  \"format\": \"bespokesynth-wasm-state\",\n"
   "  \"version\": 4,\n"
   "  \"modules\": [\n"
   "    { \"id\": 1, \"type\": \"mixer\", \"position\": { \"x\": 0, \"y\": 0 },\n"
   "      \"controls\": { \"level1\": 0.5, \"level2\": 0.8, \"level3\": 0.8, \"level4\": 0.8, \"master\": 1.0 } },\n"
   "    { \"id\": 2, \"type\": \"output\", \"position\": { \"x\": 0, \"y\": 0 }, \"controls\": { \"level\": 0.0 } }\n"
   "  ],\n"
   "  \"connections\": []\n"
   "}\n";

   ModuleCanvas::StateSnapshot loaded;
   int viewMode = 0;
   std::string error;
   TEST("V4 patch without pan deserializes", deserializePatchState(v4Json, loaded, viewMode, error));
   TEST("V4 patch migrates to the current schema", loaded.schemaVersion == kPatchSchemaVersion);
   bool centred = loaded.modules.size() == 2;
   for (const char* pan : { "pan1", "pan2", "pan3", "pan4" })
      centred = centred && loaded.modules[0].controls.count(pan) && loaded.modules[0].controls[pan] == 0.5f;
   TEST("V4 mixer loads with every pan centred", centred);
   TEST("V4 mixer keeps its levels", loaded.modules.size() == 2 && loaded.modules[0].controls["level1"] == 0.5f);
   TEST("V4 output gains limit on", loaded.modules.size() == 2 && loaded.modules[1].controls["limit"] == 1.0f);
   TEST("V4 output keeps the DC blocker off", loaded.modules.size() == 2 && loaded.modules[1].controls["dcblock"] == 0.0f);

   // A v5 patch without the control takes the new default.
   std::string v5Json = v4Json;
   v5Json.replace(v5Json.find("\"version\": 4"), 12, "\"version\": 5");
   ModuleCanvas::StateSnapshot current;
   TEST("V5 patch deserializes", deserializePatchState(v5Json, current, viewMode, error));
   TEST("V5 output defaults the DC blocker on",
        current.modules.size() == 2 && current.modules[1].controls["dcblock"] == 1.0f);
}
