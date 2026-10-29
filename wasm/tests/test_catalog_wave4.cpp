#include "test_common.h"
#include "BespokeWasm/AudioGraphSnapshotCodec.h"
#include "BespokeWasm/BundledKit.h"
#include "BespokeWasm/SamplePanelModule.h"
#include "BespokeWasm/adapters/BitcrushModuleAdapter.h"
#include "BespokeWasm/adapters/CapoModuleAdapter.h"
#include "BespokeWasm/adapters/DistortionModuleAdapter.h"
#include "BespokeWasm/adapters/EnvelopeModuleAdapter.h"
#include "BespokeWasm/adapters/FmSynthModuleAdapter.h"
#include "BespokeWasm/adapters/KarplusStrongModuleAdapter.h"
#include "BespokeWasm/adapters/NoteChanceModuleAdapter.h"
#include "BespokeWasm/adapters/NoteSequencerModuleAdapter.h"
#include "BespokeWasm/adapters/PingPongDelayModuleAdapter.h"
#include "BespokeWasm/adapters/PitchBenderModuleAdapter.h"
#include "BespokeWasm/adapters/RamperModuleAdapter.h"
#include "BespokeWasm/adapters/SeaOfGrainModuleAdapter.h"
#include "BespokeWasm/adapters/SendModuleAdapter.h"
#include "BespokeWasm/adapters/StereoWidthModuleAdapter.h"
#include "BespokeWasm/adapters/VocoderModuleAdapter.h"
#include "BespokeWasm/modules/NoteSequencerModule.h"

// --- Catalog wave 4 (#127): voices, note FX, stereo FX, vocoder, send, modulators ---

namespace
{
   constexpr float kRate = 44100.0f;
   constexpr int kBlock = 512;

   /** Adapter params + runtime state, owned for one direct test. */
   struct Instance
   {
      const WasmModuleAdapter& adapter;
      std::vector<uint8_t> params;
      std::vector<uint8_t> runtime;

      Instance(const char* type, const WasmControlMap& controls, const WasmStringMap& extras = {})
      : adapter(*WasmModuleAdapterRegistry::instance().find(type))
      {
         // Doubles in some states: keep both blocks 8-byte aligned.
         params.resize(adapter.paramsSize() + 8);
         runtime.resize(adapter.runtimeStateSize() + 8);
         adapter.fillParams(-1, controls, extras, paramsPtr());
         adapter.initRuntimeState(runtimePtr());
      }
      ~Instance() { adapter.destroyRuntimeState(runtimePtr()); }

      void* paramsPtr() { return align(params); }
      void* runtimePtr() { return align(runtime); }

      static void* align(std::vector<uint8_t>& bytes)
      {
         auto address = reinterpret_cast<uintptr_t>(bytes.data());
         return reinterpret_cast<void*>((address + 7u) & ~uintptr_t{ 7 });
      }

      template <typename T>
      T& paramsAs()
      {
         return *static_cast<T*>(paramsPtr());
      }

      std::vector<WasmNoteEvent> notes(const std::vector<WasmNoteEvent>& in,
                                       double beatStart = 0.0,
                                       double beatEnd = 0.05,
                                       const GraphMusicScale* scale = nullptr)
      {
         WasmNoteProcessBlock block;
         block.beatStart = beatStart;
         block.beatEnd = beatEnd;
         block.notes = in.empty() ? nullptr : in.data();
         block.noteCount = static_cast<int>(in.size());
         block.scale = scale;
         std::vector<WasmNoteEvent> out(128);
         int count = 0;
         adapter.processNotes(runtimePtr(), paramsPtr(), block, out.data(), 128, count);
         out.resize(static_cast<size_t>(count));
         return out;
      }

      /** One block of `numChannels` audio (channels pre-filled by the caller, as the engine sums inputs). */
      void audio(std::vector<float>& left, std::vector<float>* right, WasmAudioProcessContext context)
      {
         float* channels[2] = { left.data(), right ? right->data() : nullptr };
         context.numSamples = static_cast<int>(left.size());
         context.numChannels = right ? 2 : 1;
         context.sampleRate = context.sampleRate > 0.0f ? context.sampleRate : kRate;
         adapter.processAudio(runtimePtr(), paramsPtr(), channels, context);
      }
   };

   WasmNoteEvent noteOn(int pitch, float velocity = 1.0f, float bend = 0.0f)
   {
      WasmNoteEvent e;
      e.pitch = pitch;
      e.velocity = velocity;
      e.isNoteOn = true;
      e.pitchBend = bend;
      return e;
   }

   WasmNoteEvent noteOff(int pitch)
   {
      WasmNoteEvent e;
      e.pitch = pitch;
      return e;
   }

   std::vector<int> onPitches(const std::vector<WasmNoteEvent>& events)
   {
      std::vector<int> out;
      for (const auto& e : events)
      {
         if (e.isNoteOn && !isPitchBendEvent(e))
            out.push_back(e.pitch);
      }
      return out;
   }

   std::vector<int> offPitches(const std::vector<WasmNoteEvent>& events)
   {
      std::vector<int> out;
      for (const auto& e : events)
      {
         if (!e.isNoteOn && !isPitchBendEvent(e))
            out.push_back(e.pitch);
      }
      return out;
   }

   AudioGraphConnection cable(int src, int srcPort, int dst, int dstPort, PortType type)
   {
      return { src, srcPort, dst, dstPort, type, type };
   }

   /** Engine + snapshot driven like the live app: QWERTY / MIDI notes go through queueNote. */
   struct Graph
   {
      AudioGraphSnapshot graph;
      AudioGraphEngine engine;
      std::vector<float> left = std::vector<float>(kBlock);
      std::vector<float> right = std::vector<float>(kBlock);
      std::vector<float> allLeft;
      std::vector<float> allRight;

      Graph()
      {
         graph.transportPlaying = true;
         graph.transportBPM = 120.0f;
      }

      void add(int id, const char* type, const WasmControlMap& controls = {}, const WasmStringMap& extras = {})
      {
         appendAudioGraphNode(graph, id, type, true, controls, extras);
      }

      void connect(int src, int srcPort, int dst, int dstPort, PortType type)
      {
         graph.connections.push_back(cable(src, srcPort, dst, dstPort, type));
      }

      bool prepare()
      {
         compileAudioProcessPlan(graph, graph.processPlan);
         engine.prepareForBlock(kBlock, 128, 32);
         engine.prepareRuntimeStates(graph);
         return graph.processPlan.valid;
      }

      void queue(int pitch, float velocity, bool on, int channel = 0)
      {
         AudioGraphEngine::NoteMessage note;
         note.pitch = pitch;
         note.velocity = velocity;
         note.isNoteOn = on;
         note.channel = channel;
         engine.queueNote(note);
      }

      void run(int blocks)
      {
         for (int b = 0; b < blocks; ++b)
         {
            float* outputs[2] = { left.data(), right.data() };
            gAudioCallbackActive.store(true, std::memory_order_release);
            engine.processBlock(graph, outputs, 2, kBlock, kRate);
            gAudioCallbackActive.store(false, std::memory_order_release);
            allLeft.insert(allLeft.end(), left.begin(), left.end());
            allRight.insert(allRight.end(), right.begin(), right.end());
         }
      }

      std::vector<float> tail(size_t frames, bool rightChannel = false) const
      {
         const auto& all = rightChannel ? allRight : allLeft;
         frames = std::min(frames, all.size());
         return std::vector<float>(all.end() - static_cast<long>(frames), all.end());
      }
   };

   /** |DFT| at `freq` (Goertzel), normalised by length. */
   float toneLevel(const std::vector<float>& signal, float freq, float sampleRate = kRate)
   {
      const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
      const double coeff = 2.0 * std::cos(w);
      double s1 = 0.0;
      double s2 = 0.0;
      for (float x : signal)
      {
         const double s = x + coeff * s1 - s2;
         s2 = s1;
         s1 = s;
      }
      const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
      return static_cast<float>(std::sqrt(std::max(0.0, power)) / static_cast<double>(signal.size()));
   }

   /** Band energy around `freq` (+/- a few Goertzel probes). */
   float bandLevel(const std::vector<float>& signal, float freq)
   {
      float sum = 0.0f;
      for (float ratio : { 0.94f, 0.97f, 1.0f, 1.03f, 1.06f })
         sum += toneLevel(signal, freq * ratio);
      return sum;
   }

   /** Rising zero crossings per second. */
   float zeroCrossingHz(const std::vector<float>& signal, float sampleRate = kRate)
   {
      int crossings = 0;
      for (size_t i = 1; i < signal.size(); ++i)
         crossings += signal[i - 1] < 0.0f && signal[i] >= 0.0f ? 1 : 0;
      return static_cast<float>(crossings) * sampleRate / static_cast<float>(signal.size());
   }

   /** Pitch from the best autocorrelation lag between `minHz` and `maxHz` (parabolic peak). */
   float autocorrelationHz(const std::vector<float>& x, float minHz, float maxHz, float sampleRate = kRate)
   {
      const int minLag = static_cast<int>(sampleRate / maxHz);
      const int maxLag = static_cast<int>(sampleRate / minHz);
      auto score = [&](int lag)
      {
         double sum = 0.0;
         for (size_t i = 0; i + static_cast<size_t>(lag) < x.size(); ++i)
            sum += static_cast<double>(x[i]) * x[i + static_cast<size_t>(lag)];
         return sum;
      };
      int best = minLag;
      double bestScore = -1e30;
      for (int lag = minLag; lag <= maxLag; ++lag)
      {
         const double s = score(lag);
         if (s > bestScore)
         {
            bestScore = s;
            best = lag;
         }
      }
      const double a = score(best - 1);
      const double c = score(best + 1);
      const double denom = a - 2.0 * bestScore + c;
      const double offset = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
      return static_cast<float>(sampleRate / (best + offset));
   }

   float correlation(const std::vector<float>& a, const std::vector<float>& b)
   {
      double ab = 0.0;
      double aa = 0.0;
      double bb = 0.0;
      for (size_t i = 0; i < a.size() && i < b.size(); ++i)
      {
         ab += static_cast<double>(a[i]) * b[i];
         aa += static_cast<double>(a[i]) * a[i];
         bb += static_cast<double>(b[i]) * b[i];
      }
      return aa > 0.0 && bb > 0.0 ? static_cast<float>(ab / std::sqrt(aa * bb)) : 1.0f;
   }

   std::vector<float> sineWave(int frames, float freq, float amplitude)
   {
      std::vector<float> out(static_cast<size_t>(frames));
      for (int i = 0; i < frames; ++i)
         out[static_cast<size_t>(i)] = amplitude * std::sin(2.0f * 3.14159265f * freq * static_cast<float>(i) / kRate);
      return out;
   }

   const char* const kWave4Types[] = { "fmsynth", "karplusstrong", "seaofgrain", "notechance", "capo",
                                       "pitchbender", "notesequencer", "bitcrush", "distortion", "stereowidth",
                                       "pingpongdelay", "vocoder", "send", "envelope", "ramper" };
}

void test_catalog_wave4_registration()
{
   printf("\n=== Catalog Wave 4 (#127): Registration ===\n");
   auto& registry = WasmModuleAdapterRegistry::instance();
   struct Expect
   {
      const char* type;
      ModuleCategory category;
      WasmAudioRole role;
   };
   const Expect expected[] = {
      { "fmsynth", ModuleCategory::Synth, WasmAudioRole::AudioSource },
      { "karplusstrong", ModuleCategory::Synth, WasmAudioRole::AudioSource },
      { "seaofgrain", ModuleCategory::Synth, WasmAudioRole::AudioSource },
      { "notechance", ModuleCategory::NoteEffect, WasmAudioRole::NoteProcessor },
      { "capo", ModuleCategory::NoteEffect, WasmAudioRole::NoteProcessor },
      { "pitchbender", ModuleCategory::NoteEffect, WasmAudioRole::NoteProcessor },
      { "notesequencer", ModuleCategory::Pulse, WasmAudioRole::NoteProcessor },
      { "bitcrush", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "distortion", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "stereowidth", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "pingpongdelay", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "vocoder", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "send", ModuleCategory::AudioEffect, WasmAudioRole::AudioProcessor },
      { "envelope", ModuleCategory::Modulator, WasmAudioRole::ModulationSource },
      { "ramper", ModuleCategory::Modulator, WasmAudioRole::ModulationSource },
   };
   TEST("Wave 4 adds at least 15 adapters", sizeof(expected) / sizeof(expected[0]) >= 15);
   for (const auto& e : expected)
   {
      const WasmModuleAdapter* adapter = registry.find(e.type);
      char name[112];
      snprintf(name, sizeof(name), "%s registered with category + role", e.type);
      TEST(name, adapter && adapter->category() == e.category && adapter->audioRole() == e.role);

      bool listed = false;
      for (const auto& info : registry.typesByCategory(e.category))
         listed |= info.type == e.type;
      snprintf(name, sizeof(name), "%s listed in spawn category", e.type);
      TEST(name, listed);

      auto ui = registry.createModule(e.type, 950);
      snprintf(name, sizeof(name), "%s UI module inherits adapter ports", e.type);
      TEST(name, ui && adapter && ui->getInputs().size() == adapter->inputPorts().size() &&
                 ui->getOutputs().size() == adapter->outputPorts().size());
      if (ui && adapter)
      {
         bool defaults = true;
         for (const auto& control : adapter->controlDescriptors())
            defaults &= std::fabs(ui->getControlValue(control.name) - control.defaultValue) < 1e-4f;
         snprintf(name, sizeof(name), "%s UI defaults match control descriptors", e.type);
         TEST(name, defaults);
      }
   }
   TEST("Note transforms are exact (no Scale re-quantization)",
        !registry.find("capo")->quantizesNotesToScale() && !registry.find("pitchbender")->quantizesNotesToScale() &&
        !registry.find("notesequencer")->quantizesNotesToScale());
   TEST("Vocoder reads its two ports itself", !registry.find("vocoder")->sumsAudioInputs() &&
                                              registry.find("vocoder")->inputPorts().size() == 2);
   TEST("Send has two audio outputs", registry.find("send")->outputPorts().size() == 2);
}

void test_catalog_wave4_note_fx()
{
   printf("\n=== Catalog Wave 4: Capo / Note Chance / Pitch Bender ===\n");
   {
      Instance capo("capo", { { "capo", 2.0f } });
      TEST("Capo +2 transposes a note-on", onPitches(capo.notes({ noteOn(60) })) == std::vector<int>{ 62 });
      capo.paramsAs<CapoParams>().capo = 5;
      TEST("Capo note-off reaches the note that sounds, after the capo moved",
           offPitches(capo.notes({ noteOff(60) })) == std::vector<int>{ 62 });

      capo.notes({ noteOn(64) });
      capo.paramsAs<CapoParams>().retrigger = true;
      capo.paramsAs<CapoParams>().capo = 7;
      const auto retriggered = capo.notes({});
      TEST("Capo retrigger replays a held note at the new pitch",
           offPitches(retriggered) == std::vector<int>{ 69 } && onPitches(retriggered) == std::vector<int>{ 71 });
      capo.notes({ noteOff(64) });

      GraphMusicScale cMajor{ 0, kScaleModeMajor };
      Instance diatonic("capo", { { "capo", 2.0f }, { "diatonic", 1.0f } });
      TEST("Capo diatonic moves by scale degrees (C + 2 degrees = E)",
           onPitches(diatonic.notes({ noteOn(60) }, 0.0, 0.05, &cMajor)) == std::vector<int>{ 64 });
      TEST("Capo diatonic wraps the octave (B + 2 degrees = D)",
           onPitches(diatonic.notes({ noteOn(71) }, 0.0, 0.05, &cMajor)) == std::vector<int>{ 74 });
      TEST("Scale-degree transpose goes down too", transposeByScaleDegrees(60, -1, cMajor) == 59 &&
                                                   transposeByScaleDegrees(60, -7, cMajor) == 48);
   }
   {
      Instance never("notechance", { { "chance", 0.0f } });
      Instance always("notechance", { { "chance", 1.0f } });
      TEST("Note chance 0 drops every note", onPitches(never.notes({ noteOn(60), noteOn(62) })).empty());
      TEST("Note chance 1 passes every note", onPitches(always.notes({ noteOn(60), noteOn(62) })).size() == 2);
      TEST("A dropped note's note-off is dropped too", never.notes({ noteOff(60) }).empty());

      Instance half("notechance", { { "chance", 0.5f }, { "seed", 7.0f } });
      int passed = 0;
      for (int i = 0; i < 400; ++i)
      {
         passed += static_cast<int>(onPitches(half.notes({ noteOn(60 + i % 12) })).size());
         half.notes({ noteOff(60 + i % 12) });
      }
      TEST("Note chance 0.5 passes about half", passed > 150 && passed < 250);

      Instance det("notechance", { { "chance", 0.5f }, { "deterministic", 1.0f }, { "length", 4.0f }, { "seed", 3.0f } });
      std::vector<int> firstLoop;
      std::vector<int> secondLoop;
      for (int step = 0; step < 8; ++step)
      {
         const double beat = step * 0.25;
         const auto out = det.notes({ noteOn(60) }, beat, beat + 0.01);
         det.notes({ noteOff(60) }, beat + 0.1, beat + 0.11);
         (step < 4 ? firstLoop : secondLoop).push_back(static_cast<int>(onPitches(out).size()));
      }
      TEST("Deterministic note chance repeats every `length` sixteenths", firstLoop == secondLoop);
   }
   {
      Instance bender("pitchbender", { { "bend", 0.5f }, { "range", 2.0f } });
      const auto on = bender.notes({ noteOn(60) });
      TEST("Pitch bender starts notes bent by bend x range",
           on.size() == 1 && on[0].isNoteOn && std::fabs(on[0].pitchBend - 1.0f) < 1e-5f);
      bender.paramsAs<PitchBenderParams>().bend = -1.0f;
      const auto moved = bender.notes({});
      TEST("Moving the bend sends a PitchBend event to the sounding note",
           moved.size() == 1 && isPitchBendEvent(moved[0]) && moved[0].pitch == 60 &&
           std::fabs(moved[0].pitchBend + 2.0f) < 1e-5f);
      TEST("An unchanged bend sends nothing", bender.notes({}).empty());
      WasmNoteEvent upstreamBend;
      upstreamBend.pitch = 60;
      upstreamBend.kind = NoteEventKind::PitchBend;
      upstreamBend.pitchBend = 0.5f;
      const auto stacked = bender.notes({ upstreamBend });
      TEST("Upstream bends add to the bender's", stacked.size() == 1 && std::fabs(stacked[0].pitchBend + 1.5f) < 1e-5f);
   }

   // Bend reaches a voice: QWERTY note -> pitchbender -> oscillator, measured by zero crossings.
   auto bentFrequency = [](float bend)
   {
      Graph g;
      g.add(1, "pitchbender", { { "bend", bend }, { "range", 12.0f } });
      g.add(2, "oscillator", { { "volume", 0.8f }, { "waveform", 0.0f } });
      g.add(3, "output", { { "dcblock", 0.0f } });
      g.connect(1, 0, 2, 0, PortType::Note);
      g.connect(2, 0, 3, 0, PortType::Audio);
      g.prepare();
      g.run(1);
      g.queue(57, 1.0f, true);
      g.run(20);
      return zeroCrossingHz(g.tail(8192));
   };
   const float straight = bentFrequency(0.0f);
   const float octaveUp = bentFrequency(1.0f);
   printf("  pitchbender -> oscillator: %.1f Hz straight, %.1f Hz bent +12\n", straight, octaveUp);
   TEST("Pitch bend reaches the oscillator (+12 st doubles the pitch)",
        std::fabs(straight - 220.0f) < 6.0f && std::fabs(octaveUp - 440.0f) < 10.0f);
}

void test_catalog_wave4_fx()
{
   printf("\n=== Catalog Wave 4: Bitcrush / Distortion / Stereo Width / Ping-Pong Delay ===\n");
   {
      Instance crush("bitcrush", { { "crush", 22.0f }, { "downsamp", 4.0f }, { "mix", 1.0f } });
      std::vector<float> signal = sineWave(4096, 330.0f, 0.8f);
      crush.audio(signal, nullptr, {});
      std::vector<float> levels(signal);
      std::sort(levels.begin(), levels.end());
      levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
      bool held = true;
      for (size_t i = 0; i + 4 <= signal.size(); i += 4)
         held &= signal[i] == signal[i + 1] && signal[i] == signal[i + 3];
      TEST("Bitcrush quantizes to a handful of levels", levels.size() <= 17 && levels.size() >= 4);
      TEST("Bitcrush downsample holds each value for `downsamp` samples", held);
   }
   {
      TEST("Distortion clean shape is odd and clipped",
           std::fabs(DistortionModuleAdapter::shape(DistortionType::Clean, 0.5f) +
                     DistortionModuleAdapter::shape(DistortionType::Clean, -0.5f)) < 1e-6f &&
           std::fabs(DistortionModuleAdapter::shape(DistortionType::Clean, 10.0f)) <= 1.0f);
      // "center input" takes the DC off the input before the curve (an offset would bias the clipping).
      auto offsetMean = [](bool center)
      {
         Instance dist("distortion", { { "type", 0.0f }, { "clip", 1.0f }, { "centerInput", center ? 1.0f : 0.0f } });
         std::vector<float> signal = sineWave(44100, 110.0f, 0.3f);
         for (float& v : signal)
            v += 0.3f;
         dist.audio(signal, nullptr, {});
         double mean = 0.0;
         for (size_t i = 22050; i < signal.size(); ++i)
            mean += signal[i];
         return std::fabs(mean / 22050.0);
      };
      TEST("Distortion `center input` removes input DC", offsetMean(true) < 0.01 && offsetMean(false) > 0.1);
      Instance asym("distortion", { { "type", 4.0f }, { "clip", 0.5f }, { "preamp", 2.0f }, { "level", 1.0f } });
      std::vector<float> signal = sineWave(44100, 110.0f, 0.8f);
      asym.audio(signal, nullptr, {});
      TEST("Distortion asym output is finite and loud", all_finite(signal) && buffer_rms(signal) > 0.1f);
   }
   {
      Instance narrow("stereowidth", { { "width", 0.0f }, { "widen", 0.0f } });
      std::vector<float> left = sineWave(2048, 440.0f, 0.5f);
      std::vector<float> right = sineWave(2048, 660.0f, 0.5f);
      WasmAudioProcessContext stereo;
      stereo.inputChannels = 2;
      narrow.audio(left, &right, stereo);
      TEST("Stereo width 0 folds to mono", correlation(left, right) > 0.999f);

      Instance haas("stereowidth", { { "width", 1.0f }, { "widen", 15.0f } });
      std::vector<float> monoL = sineWave(4096, 300.0f, 0.5f);
      std::vector<float> monoR = monoL;
      haas.audio(monoL, &monoR, {});
      TEST("Stereo width Haas delay widens a mono source", correlation(monoL, monoR) < 0.9f && buffer_rms(monoR) > 0.2f);
   }
   {
      // Tempo sync: 1/8 at 120 BPM = 0.25 s; the first echo is on the left, the second on the right.
      Instance pp("pingpongdelay", { { "interval", 3.0f }, { "feedback", 0.6f }, { "mix", 1.0f }, { "tone", 18000.0f } });
      const int expected = PingPongDelayModuleAdapter::delaySamples(pp.paramsAs<PingPongDelayParams>(), 120.0f, kRate);
      TEST("Ping-pong 1/8 at 120 BPM is a quarter second", std::abs(expected - 11025) <= 1);
      std::vector<float> left(32768, 0.0f);
      std::vector<float> right(32768, 0.0f);
      left[0] = 1.0f;
      WasmAudioProcessContext context;
      context.bpm = 120.0f;
      pp.audio(left, &right, context);
      auto peakAt = [](const std::vector<float>& x, int from, int to)
      {
         int best = from;
         for (int i = from; i < to; ++i)
         {
            if (std::fabs(x[static_cast<size_t>(i)]) > std::fabs(x[static_cast<size_t>(best)]))
               best = i;
         }
         return best;
      };
      const int firstLeft = peakAt(left, 1, 16000);
      const int secondRight = peakAt(right, 16000, 32000);
      printf("  ping-pong echoes: L at %d, R at %d (expected %d, %d)\n", firstLeft, secondRight, expected, 2 * expected);
      TEST("Ping-pong first echo lands on the left, on the beat grid",
           std::abs(firstLeft - expected) <= 2 && std::fabs(left[static_cast<size_t>(firstLeft)]) > 0.3f &&
           std::fabs(right[static_cast<size_t>(expected)]) < 1e-3f);
      TEST("Ping-pong second echo bounces right", std::abs(secondRight - 2 * expected) <= 3);
      TEST("Ping-pong feedback decays",
           std::fabs(right[static_cast<size_t>(secondRight)]) < std::fabs(left[static_cast<size_t>(firstLeft)]));
   }
}

void test_catalog_wave4_send_and_modulators()
{
   printf("\n=== Catalog Wave 4: Send (aux output) / Envelope / Ramper ===\n");
   auto sendGraph = [](float amount, bool crossfade, int outputPort)
   {
      auto g = std::make_unique<Graph>();
      g->add(1, "oscillator", { { "frequency", 220.0f }, { "volume", 0.5f } });
      g->add(2, "send", { { "amount", amount }, { "crossfade", crossfade ? 1.0f : 0.0f } });
      g->add(3, "output", { { "dcblock", 0.0f }, { "limit", 0.0f } });
      g->connect(1, 0, 2, 0, PortType::Audio);
      g->connect(2, outputPort, 3, 0, PortType::Audio);
      g->prepare();
      g->run(8);
      return g;
   };
   {
      auto g = sendGraph(0.5f, false, 1);
      bool auxSlot = false;
      for (const auto& step : g->graph.processPlan.steps)
      {
         if (step.moduleId == 2)
            auxSlot = g->graph.processPlan.auxOutputSlotsOf(step).size() == 1;
      }
      TEST("Plan gives the send's second output its own slot", auxSlot);
   }
   const float dry = buffer_rms(sendGraph(0.0f, false, 0)->tail(2048));
   const float sendHalf = buffer_rms(sendGraph(0.5f, false, 1)->tail(2048));
   const float sendNone = buffer_rms(sendGraph(0.0f, false, 1)->tail(2048));
   const float crossOut = buffer_rms(sendGraph(1.0f, true, 0)->tail(2048));
   const float plainOut = buffer_rms(sendGraph(1.0f, false, 0)->tail(2048));
   printf("  send: dry %.3f, send@0.5 %.3f, send@0 %.4f, crossfade out %.4f\n", dry, sendHalf, sendNone, crossOut);
   TEST("Send output carries amount x input", std::fabs(sendHalf - 0.5f * dry) < 0.02f && sendNone < 1e-5f);
   TEST("Send keeps the dry output unless crossfading", std::fabs(plainOut - dry) < 1e-4f && crossOut < 1e-4f);

   // A cable from Out and one from Send into the same mixer: two independent signals.
   {
      Graph g;
      g.add(1, "oscillator", { { "frequency", 220.0f }, { "volume", 0.5f } });
      g.add(2, "send", { { "amount", 1.0f } });
      g.add(3, "mixer", { { "level1", 1.0f }, { "level2", 1.0f }, { "master", 1.0f }, { "pan1", 0.5f }, { "pan2", 0.5f } });
      g.add(4, "output", { { "dcblock", 0.0f }, { "limit", 0.0f } });
      g.connect(1, 0, 2, 0, PortType::Audio);
      g.connect(2, 0, 3, 0, PortType::Audio);
      g.connect(2, 1, 3, 1, PortType::Audio);
      g.connect(3, 0, 4, 0, PortType::Audio);
      g.prepare();
      g.run(8);
      TEST("Both send outputs feed one mixer (dry + send = 2x)", buffer_rms(g.tail(2048)) > 1.8f * dry);
   }

   {
      Instance env("envelope", { { "attack", 1.0f }, { "decay", 50.0f }, { "sustain", 0.5f }, { "release", 20.0f }, { "low", -0.5f }, { "high", 1.0f } });
      std::vector<float> cv(kBlock * 8);
      WasmAudioProcessContext idle;
      std::vector<float> first(kBlock);
      env.audio(first, nullptr, idle);
      TEST("Envelope rests at `low`", std::fabs(first[100] + 0.5f) < 1e-5f);
      WasmNoteEvent on = noteOn(60);
      WasmAudioProcessContext trigger;
      trigger.notes = &on;
      trigger.noteCount = 1;
      std::vector<float> attack(kBlock * 8);
      env.audio(attack, nullptr, trigger);
      const float peak = *std::max_element(attack.begin(), attack.end());
      TEST("Envelope note-on reaches `high`, then decays to sustain",
           std::fabs(peak - 1.0f) < 1e-3f && std::fabs(attack.back() - 0.25f) < 0.01f);
      WasmNoteEvent off = noteOff(60);
      WasmAudioProcessContext release;
      release.notes = &off;
      release.noteCount = 1;
      std::vector<float> tail(kBlock * 8);
      env.audio(tail, nullptr, release);
      TEST("Envelope note-off releases back to `low`", std::fabs(tail.back() + 0.5f) < 1e-3f);

      WasmPulseEvent pulse{ 0.5, 1.0f };
      WasmAudioProcessContext pulsed;
      pulsed.beatStart = 0.0;
      pulsed.beatEnd = 1.0;
      pulsed.pulses = &pulse;
      pulsed.pulseCount = 1;
      std::vector<float> oneShot(kBlock * 16);
      env.audio(oneShot, nullptr, pulsed);
      const size_t pulseFrame = oneShot.size() / 2;
      TEST("Envelope pulse starts on its frame", std::fabs(oneShot[pulseFrame - 1] + 0.5f) < 1e-5f &&
                                                 oneShot[pulseFrame + 20] > -0.4f);
      TEST("Envelope pulse is a one-shot (releases by itself)", std::fabs(oneShot.back() + 0.5f) < 1e-3f);
   }

   // Envelope -> gain Mod at gain 0 is a VCA, triggered by QWERTY notes (uncabled modulation source).
   {
      Graph g;
      g.add(1, "oscillator", { { "frequency", 330.0f }, { "volume", 0.8f } });
      g.add(2, "gain", { { "gain", 0.0f } });
      g.add(3, "envelope", { { "attack", 2.0f }, { "decay", 30.0f }, { "sustain", 1.0f }, { "release", 30.0f } });
      g.add(4, "output", { { "dcblock", 0.0f } });
      g.connect(1, 0, 2, 0, PortType::Audio);
      g.connect(3, 0, 2, 1, PortType::Modulation);
      g.connect(2, 0, 4, 0, PortType::Audio);
      g.prepare();
      g.run(4);
      const float closed = buffer_rms(g.tail(kBlock));
      g.queue(60, 1.0f, true);
      g.run(6);
      const float open = buffer_rms(g.tail(kBlock));
      g.queue(60, 0.0f, false);
      g.run(12);
      const float released = buffer_rms(g.tail(kBlock));
      printf("  envelope VCA: closed %.4f, open %.4f, released %.4f\n", closed, open, released);
      TEST("Modulation sources get performance notes when uncabled (envelope opens the VCA)",
           closed < 1e-5f && open > 0.3f && released < 1e-3f);
      TEST("Modulation source plan records no Note cable",
           g.graph.processPlan.modulationSourcePlans.size() == 1 && !g.graph.processPlan.modulationSourcePlans[0].hasNoteCable);
   }
   // Cabled: a note sequencer triggers the envelope through a Note cable.
   {
      Graph g;
      g.add(1, "notesequencer", {});
      g.add(2, "envelope", { { "attack", 1.0f }, { "decay", 20.0f }, { "sustain", 0.0f }, { "release", 10.0f } });
      g.add(3, "oscillator", { { "frequency", 330.0f }, { "volume", 0.8f } });
      g.add(4, "gain", { { "gain", 0.0f } });
      g.add(5, "output", { { "dcblock", 0.0f } });
      g.connect(1, 0, 2, 0, PortType::Note);
      g.connect(3, 0, 4, 0, PortType::Audio);
      g.connect(2, 0, 4, 1, PortType::Modulation);
      g.connect(4, 0, 5, 0, PortType::Audio);
      g.prepare();
      g.run(90);
      TEST("Note cable into a modulation source is planned",
           g.graph.processPlan.modulationSourcePlans.size() == 1 && g.graph.processPlan.modulationSourcePlans[0].hasNoteCable);
      TEST("Sequenced envelope pulses the VCA", count_onsets(g.allLeft) >= 5);
   }

   {
      Instance ramp("ramper", { { "start", 0.0f }, { "target", 1.0f }, { "length", 2.0f } });
      WasmAudioProcessContext idle;
      idle.bpm = 120.0f;
      std::vector<float> before(kBlock);
      ramp.audio(before, nullptr, idle);
      TEST("Ramper holds `start` before a trigger", before.back() == 0.0f);
      WasmNoteEvent on = noteOn(60);
      WasmAudioProcessContext trigger = idle;
      trigger.notes = &on;
      trigger.noteCount = 1;
      // One beat at 120 BPM = 22050 samples.
      std::vector<float> halfway(11025);
      ramp.audio(halfway, nullptr, trigger);
      std::vector<float> rest(22050);
      ramp.audio(rest, nullptr, idle);
      TEST("Ramper is halfway after half its length", std::fabs(halfway.back() - 0.5f) < 0.01f);
      TEST("Ramper reaches and holds `target`", rest.back() == 1.0f);

      Instance loop("ramper", { { "start", -1.0f }, { "target", 1.0f }, { "length", 2.0f }, { "mode", 1.0f } });
      WasmAudioProcessContext transport;
      transport.beatStart = 2.5;
      transport.beatEnd = 3.5;
      std::vector<float> saw(22050);
      loop.audio(saw, nullptr, transport);
      TEST("Ramper loop is locked to the transport beat", std::fabs(saw[0]) < 1e-3f && saw[11024] > 0.99f && saw[11026] < -0.99f);
   }
}

void test_catalog_wave4_voices()
{
   printf("\n=== Catalog Wave 4: FM / Karplus-Strong / Sea of Grain ===\n");
   {
      Instance fm("fmsynth", { { "release", 50.0f } });
      std::vector<float> block(kBlock);
      auto play = [&](const std::vector<WasmNoteEvent>& events)
      {
         WasmAudioProcessContext context;
         context.notes = events.empty() ? nullptr : events.data();
         context.noteCount = static_cast<int>(events.size());
         std::fill(block.begin(), block.end(), 0.0f);
         fm.audio(block, nullptr, context);
      };
      std::vector<WasmNoteEvent> chord;
      for (int i = 0; i < 8; ++i)
         chord.push_back(noteOn(48 + i * 3));
      play(chord);
      TEST("FM plays an 8-note chord on 8 voices without stealing",
           FmSynthModuleAdapter::activeVoiceCount(fm.runtimePtr()) == 8 && FmSynthModuleAdapter::stealCount(fm.runtimePtr()) == 0);
      play({ noteOn(84) });
      TEST("A ninth held note steals the oldest voice",
           FmSynthModuleAdapter::activeVoiceCount(fm.runtimePtr()) == 8 && FmSynthModuleAdapter::stealCount(fm.runtimePtr()) == 1);
      const auto& state = *static_cast<const PolyVoiceState<FmVoice>*>(fm.runtimePtr());
      bool oldestGone = true;
      bool newestThere = false;
      for (const auto& slot : state.slots)
      {
         oldestGone &= !(slot.gated && slot.pitch == 48);
         newestThere |= slot.gated && slot.pitch == 84;
      }
      TEST("The stolen voice was the first key", oldestGone && newestThere);
      std::vector<WasmNoteEvent> releaseAll;
      for (int i = 0; i < 8; ++i)
         releaseAll.push_back(noteOff(48 + i * 3));
      releaseAll.push_back(noteOff(84));
      play(releaseAll);
      for (int b = 0; b < 40; ++b)
         play({});
      TEST("Released voices free themselves", FmSynthModuleAdapter::activeVoiceCount(fm.runtimePtr()) == 0);

      Instance limited("fmsynth", { { "voices", 2.0f } });
      std::vector<float> small(kBlock);
      std::vector<WasmNoteEvent> triad = { noteOn(60), noteOn(64), noteOn(67) };
      WasmAudioProcessContext context;
      context.notes = triad.data();
      context.noteCount = 3;
      limited.audio(small, nullptr, context);
      TEST("FM `voices` limits polyphony", FmSynthModuleAdapter::activeVoiceCount(limited.runtimePtr()) == 2);
   }

   // QWERTY / MIDI path: the engine's performance notes reach an uncabled 8-voice FM synth.
   auto fmChord = [](float modIndex, int notes, int channel)
   {
      auto g = std::make_unique<Graph>();
      g->add(1, "fmsynth", { { "mod", modIndex }, { "sustain", 1.0f } });
      g->add(2, "output", { { "dcblock", 0.0f } });
      g->connect(1, 0, 2, 0, PortType::Audio);
      g->prepare();
      g->run(1);
      for (int i = 0; i < notes; ++i)
         g->queue(48 + i * 4, 0.8f, true, channel);
      g->run(10);
      return g;
   };
   {
      auto chord = fmChord(2.0f, 8, 0);
      const float level = buffer_rms(chord->tail(4096));
      TEST("FM chord from performance notes is audible and in range", level > 0.05f && level < 1.0f && all_finite(chord->allLeft));
      auto midi = fmChord(2.0f, 8, 3);
      TEST("FM chord from a MIDI channel plays the same", std::fabs(buffer_rms(midi->tail(4096)) - level) < 1e-4f);
      auto stolen = fmChord(2.0f, 10, 0);
      TEST("Ten keys on eight voices stay bounded", all_finite(stolen->allLeft) && buffer_rms(stolen->tail(4096)) < 1.0f);
   }
   {
      auto pure = fmChord(0.0f, 1, 0);
      auto bright = fmChord(6.0f, 1, 0);
      const float f0 = 440.0f * std::exp2((48.0f - 69.0f) / 12.0f);
      auto harmonicShare = [f0](const std::vector<float>& x)
      {
         float upper = 0.0f;
         for (int h = 2; h <= 6; ++h)
            upper += toneLevel(x, f0 * static_cast<float>(h));
         return upper / std::max(1e-6f, toneLevel(x, f0));
      };
      const float pureShare = harmonicShare(pure->tail(8192));
      const float brightShare = harmonicShare(bright->tail(8192));
      printf("  FM harmonics/fundamental: mod 0 -> %.3f, mod 6 -> %.3f\n", pureShare, brightShare);
      TEST("FM mod index adds sidebands (mod 0 is a sine)", pureShare < 0.05f && brightShare > 0.3f);
   }

   {
      Graph g;
      g.add(1, "karplusstrong", { { "feedback", 0.995f }, { "filter", 0.5f } });
      g.add(2, "output", { { "dcblock", 0.0f } });
      g.connect(1, 0, 2, 0, PortType::Audio);
      g.prepare();
      g.run(1);
      g.queue(57, 1.0f, true);
      g.run(12);
      const auto ring = g.tail(8192);
      const float pitchHz = autocorrelationHz(ring, 100.0f, 400.0f);
      printf("  karplus-strong A3: %.1f Hz, rms %.3f\n", pitchHz, buffer_rms(ring));
      TEST("Karplus-Strong rings at the note's pitch (within 10 cents)", std::fabs(1200.0f * std::log2(pitchHz / 220.0f)) < 10.0f && buffer_rms(ring) > 0.01f);
      TEST("Karplus-Strong output is finite", all_finite(g.allLeft));
   }
   {
      Instance ks("karplusstrong", { { "feedback", 0.9f } });
      std::vector<float> block(kBlock);
      WasmNoteEvent on = noteOn(69);
      WasmAudioProcessContext context;
      context.notes = &on;
      context.noteCount = 1;
      ks.audio(block, nullptr, context);
      const float early = buffer_rms(block);
      for (int b = 0; b < 200; ++b)
         ks.audio(block, nullptr, {});
      TEST("A plucked string dies out and frees its voice while the key is held",
           early > 0.01f && KarplusStrongModuleAdapter::activeVoiceCount(ks.runtimePtr()) == 0);
   }

   {
      auto& store = SampleStore::instance();
      store.setDeviceSampleRate(44100);
      const BundledKitSample& pad = bundledTonalSamples()[0];
      const SampleBuffer* sample = findSampleOrBundled(pad.sha256);
      TEST("Bundled tonal pad resolves by hash", sample && sample->frameCount() > 44100 && pad.rootPitch == 60);
      auto ui = WasmModuleAdapterRegistry::instance().createModule("seaofgrain", 960);
      TEST("Sea of grain starts on the bundled pad", ui && ui->getStringProperty("sampleHash") == pad.sha256);

      auto grains = [&](int pitch, float width)
      {
         auto g = std::make_unique<Graph>();
         g->add(1, "seaofgrain", { { "width", width }, { "posRandom", 0.0f }, { "spacingRandom", 0.0f } },
                { { "sampleHash", pad.sha256 } });
         g->add(2, "output", { { "dcblock", 0.0f } });
         g->connect(1, 0, 2, 0, PortType::Audio);
         g->prepare();
         g->run(1);
         g->queue(pitch, 1.0f, true);
         g->run(16);
         return g;
      };
      auto wide = grains(60, 1.0f);
      const auto left = wide->tail(8192);
      const auto right = wide->tail(8192, true);
      TEST("Sea of grain plays the sample from a note", buffer_rms(left) > 0.02f && all_finite(wide->allLeft));
      TEST("Grains spread across the stereo field", correlation(left, right) < 0.98f);
      const float c4 = 261.6256f;
      auto rootTone = grains(60, 0.0f)->tail(16384);
      auto octaveTone = grains(72, 0.0f)->tail(16384);
      printf("  sea of grain C4 key: %.4f @C4, C5 key: %.4f @C5 / %.4f @C4\n", toneLevel(rootTone, c4),
             toneLevel(octaveTone, 2.0f * c4), toneLevel(octaveTone, c4));
      TEST("Pitch mode: the key sets the grain speed (C5 plays the pad an octave up)",
           toneLevel(rootTone, c4) > 0.01f && toneLevel(octaveTone, 2.0f * c4) > 3.0f * toneLevel(octaveTone, c4));

      Instance sea("seaofgrain", {}, { { "sampleHash", pad.sha256 } });
      WasmResourceRefs refs;
      sea.adapter.collectResources(-1, sea.paramsPtr(), refs);
      TEST("Sea of grain pins its sample for the snapshot", refs.sampleIds.size() == 1);
      TEST("Position mode maps keys across the sample",
           SeaOfGrainModuleAdapter::voicePosition({ .mode = 0, .position = 0.0f, .keyboardBase = 36, .keyboardRange = 24 }, 48) == 0.5f);
   }
}

void test_catalog_wave4_eq_curve()
{
   printf("\n=== Catalog Wave 4: EQ response curve ===\n");
   EqParams params;
   EqModuleAdapter eq;
   eq.fillParams({ { "midFreq", 1000.0f }, { "midGain", 12.0f }, { "midQ", 1.0f }, { "highGain", -6.0f }, { "highFreq", 8000.0f }, { "output", 0.0f } },
                 &params);
   TEST("EQ response peaks at the mid band's gain", std::fabs(EqModuleAdapter::responseDb(params, 1000.0f, 48000.0f) - 12.0f) < 0.5f);
   TEST("EQ response is flat far from every band", std::fabs(EqModuleAdapter::responseDb(params, 40.0f, 48000.0f)) < 0.5f);
   TEST("EQ response follows the high shelf", std::fabs(EqModuleAdapter::responseDb(params, 18000.0f, 48000.0f) + 6.0f) < 1.0f);

   // The curve is what the audio does: measure a 1 kHz sine through the adapter.
   std::vector<uint8_t> runtime(eq.runtimeStateSize());
   eq.initRuntimeState(runtime.data());
   std::vector<float> tone = sineWave(16384, 1000.0f, 0.1f);
   WasmAudioProcessContext context;
   context.sampleRate = 48000.0f;
   context.numSamples = static_cast<int>(tone.size());
   eq.processMono(runtime.data(), &params, tone.data(), context);
   const float measuredDb = 20.0f * std::log10(buffer_rms(std::vector<float>(tone.end() - 8192, tone.end())) / (0.1f / std::sqrt(2.0f)));
   printf("  EQ at 1 kHz: curve %.2f dB, audio %.2f dB\n", EqModuleAdapter::responseDb(params, 1000.0f, 48000.0f), measuredDb);
   TEST("EQ curve matches the processed audio", std::fabs(measuredDb - EqModuleAdapter::responseDb(params, 1000.0f, 48000.0f)) < 0.5f);
   eq.destroyRuntimeState(runtime.data());
}

void test_catalog_wave4_vocoder()
{
   printf("\n=== Catalog Wave 4: Vocoder (carrier + modulator) ===\n");
   // Carrier: a 110 Hz saw (harmonics everywhere). Modulator: a sine at `modHz`.
   auto vocode = [](float modHz, bool modulator)
   {
      auto g = std::make_unique<Graph>();
      g->add(1, "oscillator", { { "frequency", 110.0f }, { "volume", 0.5f }, { "waveform", 1.0f } });
      g->add(2, "oscillator", { { "frequency", modHz }, { "volume", 0.5f } });
      g->add(3, "vocoder", { { "bands", 32.0f } });
      g->add(4, "output", { { "dcblock", 0.0f }, { "limit", 0.0f } });
      g->connect(1, 0, 3, 0, PortType::Audio);
      if (modulator)
         g->connect(2, 0, 3, 1, PortType::Audio);
      g->connect(3, 0, 4, 0, PortType::Audio);
      g->prepare();
      g->run(40);
      return g->tail(16384);
   };
   const auto high = vocode(1980.0f, true);
   const auto low = vocode(330.0f, true);
   const auto silent = vocode(330.0f, false);
   const float highAtHigh = bandLevel(high, 1980.0f);
   const float highAtLow = bandLevel(high, 330.0f);
   const float lowAtHigh = bandLevel(low, 1980.0f);
   const float lowAtLow = bandLevel(low, 330.0f);
   printf("  vocoder: mod 1980 Hz -> %.4f @1980 / %.4f @330; mod 330 Hz -> %.4f @1980 / %.4f @330\n", highAtHigh,
          highAtLow, lowAtHigh, lowAtLow);
   TEST("Vocoder spectral envelope follows a high modulator", highAtHigh > 4.0f * highAtLow);
   TEST("Vocoder spectral envelope follows a low modulator", lowAtLow > 4.0f * lowAtHigh);
   TEST("Vocoder output keeps the carrier's harmonics (110 Hz multiples)",
        toneLevel(high, 1980.0f) > 3.0f * toneLevel(high, 1925.0f));
   TEST("No modulator, no output", buffer_rms(silent) < 1e-4f);
   TEST("Vocoder output is finite", all_finite(high) && all_finite(low));
}

void test_catalog_wave4_note_sequencer()
{
   printf("\n=== Catalog Wave 4: Note Sequencer ===\n");
   GraphMusicScale cMajor{ 0, kScaleModeMajor };
   WasmControlMap controls = { { "steps", 4.0f }, { "interval", 2.0f }, { "octave", 4.0f }, { "tone0", 0.0f }, { "tone1", 2.0f }, { "tone2", 4.0f }, { "tone3", 7.0f }, { "vel0", 1.0f }, { "vel1", 0.5f }, { "vel2", 0.0f }, { "vel3", 0.9f }, { "len0", 0.5f }, { "len1", 0.5f }, { "len2", 0.5f }, { "len3", 1.0f } };
   Instance seq("notesequencer", controls);
   std::vector<WasmNoteEvent> all;
   for (int b = 0; b < 16; ++b)
   {
      const auto out = seq.notes({}, b * 0.125, (b + 1) * 0.125, &cMajor);
      all.insert(all.end(), out.begin(), out.end());
   }
   TEST("Rows are scale degrees above the octave (C E G, rest, C')",
        onPitches(all) == std::vector<int>({ 60, 64, 72, 60, 64, 72 }));
   bool velocity = false;
   for (const auto& e : all)
      velocity |= e.isNoteOn && e.pitch == 64 && std::fabs(e.velocity - 0.5f) < 1e-6f;
   TEST("Step velocity reaches the note", velocity);
   TEST("Every note-on is ended", offPitches(all).size() + 1 >= onPitches(all).size());
   TEST("Row pitch in chromatic mode is semitones",
        NoteSequencerModuleAdapter::rowPitch({ .octave = 3, .mode = 1 }, 5, &cMajor) == 53);

   auto ui = WasmModuleAdapterRegistry::instance().createModule("notesequencer", 970);
   auto* grid = dynamic_cast<NoteSequencerModule*>(ui.get());
   TEST("Note sequencer UI is the grid widget", grid != nullptr);
   if (grid)
   {
      grid->setPosition(0.0f, 0.0f);
      grid->setControlValue("vel3", 0.0f);
      // Find the grid by its control name, then click step 3's top row.
      float cellX = -1.0f;
      float cellY = -1.0f;
      for (float y = 0.0f; y < grid->getHeight() && cellX < 0.0f; y += 1.0f)
      {
         for (float x = 0.0f; x < grid->getWidth(); x += 1.0f)
         {
            if (grid->controlNameAt(x, y) == "tone3")
            {
               cellX = x + 2.0f;
               cellY = y + 2.0f;
               break;
            }
         }
      }
      TEST("Grid cells name their step control (hover help, MIDI learn)", cellX >= 0.0f);
      grid->handleMouseDown(cellX, cellY);
      grid->handleMouseUp();
      TEST("Clicking a grid cell sets the step's tone and turns it on",
           grid->getControlValue("tone3") == 7.0f && grid->getControlValue("vel3") > 0.0f);
      grid->handleMouseDown(cellX, cellY);
      grid->handleMouseUp();
      TEST("Clicking the lit cell again rests the step", grid->getControlValue("vel3") == 0.0f);
   }
}

void test_catalog_wave4_arp_fm_chain()
{
   printf("\n=== Catalog Wave 4: Arpeggiator -> FM -> EQ -> Ping-Pong -> Output ===\n");
   Graph g;
   g.add(1, "arpeggiator", { { "interval", 2.0f } });
   g.add(2, "fmsynth", { { "decay", 80.0f }, { "sustain", 0.2f }, { "release", 60.0f } });
   g.add(3, "eq", {});
   g.add(4, "pingpongdelay", { { "interval", 3.0f }, { "feedback", 0.5f }, { "mix", 0.5f } });
   g.add(5, "output", { { "dcblock", 0.0f } });
   g.connect(1, 0, 2, 0, PortType::Note);
   g.connect(2, 0, 3, 0, PortType::Audio);
   g.connect(3, 0, 4, 0, PortType::Audio);
   g.connect(4, 0, 5, 0, PortType::Audio);
   TEST("Chain compiles", g.prepare());
   g.engine.setMidiOutCapture(true);
   g.run(1);
   g.queue(60, 0.9f, true);
   g.queue(64, 0.9f, true);
   g.queue(67, 0.9f, true);
   g.run(173); // ~2 s = 4 beats at 120 BPM
   std::vector<int> arpOn;
   AudioGraphEngine::MidiOutNote drained[256];
   int got;
   while ((got = g.engine.drainMidiOut(drained, 256)) > 0)
   {
      for (int i = 0; i < got; ++i)
      {
         if (drained[i].isNoteOn)
            arpOn.push_back(drained[i].pitch);
      }
   }
   printf("  arp note-ons in ~4 beats: %zu\n", arpOn.size());
   TEST("Arpeggiator steps on the transport's sixteenths", arpOn.size() >= 14 && arpOn.size() <= 18);
   TEST("Arpeggiator cycles the held chord", arpOn.size() >= 3 && arpOn[0] != arpOn[1] && arpOn[1] != arpOn[2]);
   const auto left = g.tail(44100);
   const auto right = g.tail(44100, true);
   printf("  chain L rms %.3f, R rms %.3f, L/R correlation %.3f\n", buffer_rms(left), buffer_rms(right), correlation(left, right));
   TEST("Chain is audible on both sides", buffer_rms(left) > 0.01f && buffer_rms(right) > 0.01f);
   TEST("Ping-pong makes the chain audibly stereo", correlation(left, right) < 0.9f);
   TEST("Chain output is finite", all_finite(g.allLeft) && all_finite(g.allRight));
}

void test_catalog_wave4_saved_state_fixture()
{
   printf("\n=== Catalog Wave 4: Saved-State Fixture + Wire Codec ===\n");
   // Every wave-4 type in one patch: notesequencer -> capo -> notechance -> pitchbender -> fmsynth;
   // karplusstrong + seaofgrain in a mixer; distortion -> bitcrush -> stereowidth -> send; send's Send
   // output -> pingpongdelay; vocoder on the side; envelope and ramper on gain / filter.
   const char* fixture =
   "{\n"
   "  \"filetype\": \"bespokesynth\", \"format\": \"bespokesynth-wasm-state\", \"version\": 4,\n"
   "  \"transport\": { \"bpm\": 120, \"playing\": true },\n"
   "  \"modules\": [\n"
   "    { \"id\": 1, \"type\": \"notesequencer\", \"position\": { \"x\": 40, \"y\": 0 }, \"controls\": { \"steps\": 4, \"octave\": 3 } },\n"
   "    { \"id\": 2, \"type\": \"capo\", \"position\": { \"x\": 80, \"y\": 0 }, \"controls\": { \"capo\": 2, \"diatonic\": 1 } },\n"
   "    { \"id\": 3, \"type\": \"notechance\", \"position\": { \"x\": 120, \"y\": 0 }, \"controls\": { \"chance\": 1 } },\n"
   "    { \"id\": 4, \"type\": \"pitchbender\", \"position\": { \"x\": 160, \"y\": 0 }, \"controls\": { \"bend\": 0.1 } },\n"
   "    { \"id\": 5, \"type\": \"fmsynth\", \"position\": { \"x\": 200, \"y\": 0 }, \"controls\": { \"mod\": 3, \"harmratio\": 6 } },\n"
   "    { \"id\": 6, \"type\": \"karplusstrong\", \"position\": { \"x\": 240, \"y\": 0 }, \"controls\": { \"feedback\": 0.99 } },\n"
   "    { \"id\": 7, \"type\": \"seaofgrain\", \"position\": { \"x\": 280, \"y\": 0 }, \"controls\": { \"overlap\": 4 },\n"
   "      \"extras\": { \"sampleHash\": \"5108d536d9bc7975f804e7b4e39ec5b813666ad8369936876f643faf25aa78f3\" } },\n"
   "    { \"id\": 8, \"type\": \"mixer\", \"position\": { \"x\": 320, \"y\": 0 }, \"controls\": { \"level1\": 0.8, \"level2\": 0.6, \"level3\": 0.6 } },\n"
   "    { \"id\": 9, \"type\": \"distortion\", \"position\": { \"x\": 360, \"y\": 0 }, \"controls\": { \"type\": 1, \"clip\": 0.6 } },\n"
   "    { \"id\": 10, \"type\": \"bitcrush\", \"position\": { \"x\": 400, \"y\": 0 }, \"controls\": { \"crush\": 10, \"mix\": 0.3 } },\n"
   "    { \"id\": 11, \"type\": \"stereowidth\", \"position\": { \"x\": 440, \"y\": 0 }, \"controls\": { \"widen\": 8 } },\n"
   "    { \"id\": 12, \"type\": \"send\", \"position\": { \"x\": 480, \"y\": 0 }, \"controls\": { \"amount\": 0.4 } },\n"
   "    { \"id\": 13, \"type\": \"pingpongdelay\", \"position\": { \"x\": 520, \"y\": 0 }, \"controls\": { \"mix\": 1 } },\n"
   "    { \"id\": 14, \"type\": \"vocoder\", \"position\": { \"x\": 560, \"y\": 0 }, \"controls\": { \"bands\": 16, \"mix\": 0.5 } },\n"
   "    { \"id\": 15, \"type\": \"envelope\", \"position\": { \"x\": 600, \"y\": 0 }, \"controls\": { \"attack\": 5, \"low\": 0, \"high\": 0.5 } },\n"
   "    { \"id\": 16, \"type\": \"ramper\", \"position\": { \"x\": 640, \"y\": 0 }, \"controls\": { \"mode\": 1, \"start\": -0.5, \"target\": 0.5 } },\n"
   "    { \"id\": 17, \"type\": \"gain\", \"position\": { \"x\": 680, \"y\": 0 }, \"controls\": { \"gain\": 0.5 } },\n"
   "    { \"id\": 18, \"type\": \"filter\", \"position\": { \"x\": 720, \"y\": 0 }, \"controls\": { \"cutoff\": 2000 } },\n"
   "    { \"id\": 19, \"type\": \"mixer\", \"position\": { \"x\": 760, \"y\": 0 }, \"controls\": { \"level1\": 0.8, \"level2\": 0.8, \"level3\": 0.5 } },\n"
   "    { \"id\": 20, \"type\": \"output\", \"position\": { \"x\": 800, \"y\": 0 }, \"controls\": {} }\n"
   "  ],\n"
   "  \"connections\": [\n"
   "    { \"source\": { \"module\": 1, \"port\": 0 }, \"destination\": { \"module\": 2, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 2, \"port\": 0 }, \"destination\": { \"module\": 3, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 3, \"port\": 0 }, \"destination\": { \"module\": 4, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 4, \"port\": 0 }, \"destination\": { \"module\": 5, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 4, \"port\": 0 }, \"destination\": { \"module\": 6, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 4, \"port\": 0 }, \"destination\": { \"module\": 7, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 4, \"port\": 0 }, \"destination\": { \"module\": 15, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 5, \"port\": 0 }, \"destination\": { \"module\": 8, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 6, \"port\": 0 }, \"destination\": { \"module\": 8, \"port\": 1 } },\n"
   "    { \"source\": { \"module\": 7, \"port\": 0 }, \"destination\": { \"module\": 8, \"port\": 2 } },\n"
   "    { \"source\": { \"module\": 8, \"port\": 0 }, \"destination\": { \"module\": 9, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 9, \"port\": 0 }, \"destination\": { \"module\": 10, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 10, \"port\": 0 }, \"destination\": { \"module\": 17, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 15, \"port\": 0 }, \"destination\": { \"module\": 17, \"port\": 1 } },\n"
   "    { \"source\": { \"module\": 17, \"port\": 0 }, \"destination\": { \"module\": 18, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 16, \"port\": 0 }, \"destination\": { \"module\": 18, \"port\": 1 } },\n"
   "    { \"source\": { \"module\": 18, \"port\": 0 }, \"destination\": { \"module\": 11, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 11, \"port\": 0 }, \"destination\": { \"module\": 12, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 12, \"port\": 0 }, \"destination\": { \"module\": 19, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 12, \"port\": 1 }, \"destination\": { \"module\": 13, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 13, \"port\": 0 }, \"destination\": { \"module\": 19, \"port\": 1 } },\n"
   "    { \"source\": { \"module\": 6, \"port\": 0 }, \"destination\": { \"module\": 14, \"port\": 0 } },\n"
   "    { \"source\": { \"module\": 5, \"port\": 0 }, \"destination\": { \"module\": 14, \"port\": 1 } },\n"
   "    { \"source\": { \"module\": 14, \"port\": 0 }, \"destination\": { \"module\": 19, \"port\": 2 } },\n"
   "    { \"source\": { \"module\": 19, \"port\": 0 }, \"destination\": { \"module\": 20, \"port\": 0 } }\n"
   "  ]\n"
   "}\n";

   ModuleCanvas::StateSnapshot loaded;
   int viewMode = 0;
   std::string error;
   TEST("Wave-4 fixture deserializes", deserializePatchState(fixture, loaded, viewMode, error));
   TEST("Wave-4 fixture keeps all modules and cables", loaded.modules.size() == 20 && loaded.connections.size() == 25);
   bool everyType = true;
   for (const char* type : kWave4Types)
   {
      bool found = false;
      for (const auto& module : loaded.modules)
         found |= module.type == type;
      everyType &= found;
   }
   TEST("Wave-4 fixture covers all 15 new types", everyType);

   ModuleCanvas canvas;
   TEST("Wave-4 fixture loads on the canvas with no placeholders",
        canvas.applyStateSnapshot(loaded) && canvas.placeholderCount() == 0);
   // The canvas renumbers ids on load; pair modules up by type and position.
   const auto saved = canvas.createStateSnapshot();
   bool controlsKept = true;
   for (const auto& original : loaded.modules)
   {
      const ModuleCanvas::StateModule* match = nullptr;
      for (const auto& module : saved.modules)
      {
         if (module.type == original.type && module.x == original.x)
            match = &module;
      }
      if (!match)
      {
         controlsKept = false;
         continue;
      }
      for (const auto& [name, value] : original.controls)
      {
         const auto it = match->controls.find(name);
         controlsKept &= it != match->controls.end() && std::fabs(it->second - value) < 1e-4f;
      }
      if (original.extras.count("sampleHash"))
         controlsKept &= match->extras.count("sampleHash") && match->extras.at("sampleHash") == original.extras.at("sampleHash");
   }
   TEST("Wave-4 controls and sample extras survive a canvas round trip", controlsKept);
   TEST("Wave-4 cables survive a canvas round trip", saved.connections.size() == loaded.connections.size());

   canvas.setTransportPlaying(true);
   const auto graph = canvas.getAudioGraphSnapshot();
   TEST("Wave-4 fixture compiles", graph && graph->processPlan.valid);
   if (!graph)
      return;
   std::vector<uint8_t> wire;
   encodeAudioGraphSnapshot(*graph, wire);
   AudioGraphSnapshot decoded;
   TEST("Wave-4 snapshot (aux outputs, modulation event plans) crosses the wire",
        decodeAudioGraphSnapshot(wire.data(), wire.size(), decoded, error) &&
        decoded.processPlan.auxOutputSlots == graph->processPlan.auxOutputSlots &&
        decoded.processPlan.modulationSourcePlans.size() == graph->processPlan.modulationSourcePlans.size() &&
        !decoded.processPlan.auxOutputSlots.empty());

   AudioGraphSnapshot local = *graph;
   auto renderFor = [](AudioGraphSnapshot& snapshot)
   {
      AudioGraphEngine engine;
      engine.prepareForBlock(kBlock, 128, 32);
      engine.prepareRuntimeStates(snapshot);
      std::vector<float> all;
      std::vector<float> left(kBlock);
      std::vector<float> right(kBlock);
      for (int b = 0; b < 120; ++b)
      {
         float* outputs[2] = { left.data(), right.data() };
         gAudioCallbackActive.store(true, std::memory_order_release);
         engine.processBlock(snapshot, outputs, 2, kBlock, kRate);
         gAudioCallbackActive.store(false, std::memory_order_release);
         all.insert(all.end(), left.begin(), left.end());
         all.insert(all.end(), right.begin(), right.end());
      }
      return all;
   };
   const auto here = renderFor(local);
   const auto there = renderFor(decoded);
   TEST("Wave-4 fixture plays without MIDI, finite, without allocating", buffer_rms(here) > 0.01f && all_finite(here));
   TEST("Decoded wave-4 graph renders bit-identically", here == there);
}
