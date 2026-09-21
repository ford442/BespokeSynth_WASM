#include "test_common.h"

// DSP kernels (SIMD when built with BESPOKE_WASM_SIMD) must match scalar references.
void test_dsp_simd_kernels()
{
   printf("\n=== DSP SIMD Kernel Tests (simd=%d) ===\n", dsp::kSimdEnabled ? 1 : 0);

   auto maxDiff = [](const std::vector<float>& a, const std::vector<float>& b) {
      float worst = 0.0f;
      for (size_t i = 0; i < a.size() && i < b.size(); ++i)
         worst = std::max(worst, std::fabs(a[i] - b[i]));
      return a.size() == b.size() ? worst : 1e9f;
   };

   // Odd length exercises the vector body plus the scalar tail.
   const int n = 37;
   std::vector<float> a(n + 1), b(n + 1);
   for (int i = 0; i <= n; ++i)
   {
      a[i] = std::sin(0.37f * static_cast<float>(i)) * 0.8f;
      b[i] = std::cos(0.11f * static_cast<float>(i)) - 0.25f;
   }

   {
      std::vector<float> got(a.begin(), a.begin() + n), want(got);
      dsp::scale(got.data(), n, 0.3f);
      for (float& v : want)
         v *= 0.3f;
      TEST("dsp::scale matches scalar", maxDiff(got, want) < 1e-6f);
   }
   {
      std::vector<float> got(b.begin(), b.begin() + n), want(got);
      dsp::accumulateScaled(got.data(), a.data(), n, 0.6f);
      for (int i = 0; i < n; ++i)
         want[i] += a[i] * 0.6f;
      TEST("dsp::accumulateScaled matches scalar", maxDiff(got, want) < 1e-6f);
   }
   {
      std::vector<float> got(a.begin(), a.begin() + n), want(n);
      for (int i = 0; i < n; ++i)
         want[i] = a[i] * 0.25f + b[i] * 0.75f;
      dsp::mix2(got.data(), got.data(), 0.25f, b.data(), 0.75f, n); // in-place aliasing
      TEST("dsp::mix2 matches scalar (aliased dst)", maxDiff(got, want) < 1e-6f);
   }
   {
      std::vector<float> got(n), want(n);
      dsp::lerpRead(got.data(), a.data(), n, 0.3f);
      for (int i = 0; i < n; ++i)
         want[i] = a[i] * 0.7f + a[i + 1] * 0.3f;
      TEST("dsp::lerpRead matches scalar", maxDiff(got, want) < 1e-6f);
   }
   {
      std::vector<float> got(n), want(n);
      dsp::magnitude(got.data(), a.data(), b.data(), n, 2.0f, 1.0f);
      for (int i = 0; i < n; ++i)
         want[i] = std::min(1.0f, std::sqrt(a[i] * a[i] + b[i] * b[i]) * 2.0f);
      TEST("dsp::magnitude matches scalar", maxDiff(got, want) < 1e-5f);
   }

   // Delay adapter vs the original per-sample algorithm, across ring wraps.
   for (float delayTime : { 0.001f, 0.0137f, 0.37f, 1.999f })
   {
      DelayModuleAdapter adapter;
      DelayParams params{};
      adapter.fillParams({ { "time", delayTime }, { "feedback", 0.6f }, { "mix", 0.4f } }, &params);
      std::vector<uint8_t> runtimeStorage(adapter.runtimeStateSize());
      adapter.initRuntimeState(runtimeStorage.data());

      const float sampleRate = 44100.0f;
      const size_t capacity = static_cast<size_t>(sampleRate * DelayModuleAdapter::kMaxDelaySeconds);
      std::vector<float> refRing(capacity, 0.0f);
      size_t refWrite = 0;
      const float delaySamples = delayTime * sampleRate;

      float worst = 0.0f;
      double phase = 0.0;
      const int blocks = 220; // 220 * 512 > capacity, so both heads wrap.
      for (int block = 0; block < blocks; ++block)
      {
         float buffer[512];
         float reference[512];
         for (int i = 0; i < 512; ++i)
         {
            buffer[i] = reference[i] = static_cast<float>(std::sin(phase));
            phase += 2.0 * 3.14159265358979 * 220.0 / sampleRate;
         }
         WasmAudioProcessContext context;
         context.sampleRate = sampleRate;
         context.numSamples = 512;
         adapter.processAudio(runtimeStorage.data(), &params, buffer, context);

         for (int i = 0; i < 512; ++i)
         {
            const float input = reference[i];
            float readPos = static_cast<float>(refWrite) - delaySamples;
            while (readPos < 0.0f)
               readPos += static_cast<float>(capacity);
            const size_t i0 = static_cast<size_t>(readPos) % capacity;
            const size_t i1 = (i0 + 1) % capacity;
            const float frac = readPos - std::floor(readPos);
            const float delayed = refRing[i0] * (1.0f - frac) + refRing[i1] * frac;
            refRing[refWrite] = input + delayed * 0.6f;
            refWrite = (refWrite + 1) % capacity;
            reference[i] = input * 0.6f + delayed * 0.4f;
            worst = std::max(worst, std::fabs(reference[i] - buffer[i]));
         }
      }
      char label[96];
      std::snprintf(label, sizeof(label), "Delay block kernel matches per-sample reference (%.4fs)", delayTime);
      TEST(label, worst < 2e-3f);
      adapter.destroyRuntimeState(runtimeStorage.data());
   }

   // Spectrum FFT vs a naive DFT of the same windowed input.
   {
      const int fftSize = AudioAnalysis::kFftSize;
      std::vector<float> input(fftSize);
      for (int i = 0; i < fftSize; ++i)
         input[i] = 0.5f * std::sin(2.0f * 3.14159265f * 12.0f * i / fftSize) +
                    0.3f * std::sin(2.0f * 3.14159265f * 40.0f * i / fftSize);
      AudioAnalysis::pushSamples(input.data(), fftSize);
      float spectrum[AudioAnalysis::kSpectrumBins];
      AudioAnalysis::computeSpectrum(spectrum, AudioAnalysis::kSpectrumBins);

      float worst = 0.0f;
      for (int bin = 0; bin < AudioAnalysis::kSpectrumBins; ++bin)
      {
         double re = 0.0, im = 0.0;
         for (int i = 0; i < fftSize; ++i)
         {
            const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / (fftSize - 1));
            const double angle = -2.0 * 3.14159265358979 * bin * i / fftSize;
            re += input[i] * w * std::cos(angle);
            im += input[i] * w * std::sin(angle);
         }
         const float want = std::min(1.0f, static_cast<float>(std::sqrt(re * re + im * im) * 0.004));
         worst = std::max(worst, std::fabs(want - spectrum[bin]));
      }
      TEST("Spectrum FFT matches naive DFT", worst < 1e-3f);
      TEST("Spectrum FFT peaks at the input bins", spectrum[12] > spectrum[20] && spectrum[40] > spectrum[30]);
   }
}


// ---------------------------------------------------------------------------
// Live instrument: Scala tuning, undo/redo, MIDI learn, script note queue
// ---------------------------------------------------------------------------

// 7-note just-intonation major: 5/4 third and 3/2 fifth instead of 12-TET.
static const char* kJustMajorScl =
"! just_major.scl\n"
"!\n"
"Just intonation major\n"
" 7\n"
"!\n"
" 9/8\n"
" 5/4\n"
" 4/3\n"
" 3/2\n"
" 5/3\n"
" 15/8\n"
" 2/1\n";

static float measured_frequency(const std::vector<float>& samples, float sampleRate)
{
   int crossings = 0;
   int first = -1;
   int last = -1;
   for (size_t i = 1; i < samples.size(); ++i)
   {
      if (samples[i - 1] <= 0.0f && samples[i] > 0.0f)
      {
         if (first < 0)
            first = static_cast<int>(i);
         last = static_cast<int>(i);
         ++crossings;
      }
   }
   if (crossings < 2)
      return 0.0f;
   return static_cast<float>(crossings - 1) * sampleRate / static_cast<float>(last - first);
}

static float render_tuned_note(const GraphMusicScale& scale, int pitch)
{
   AudioGraphSnapshot graph;
   graph.transportPlaying = true;
   graph.musicScale = scale;
   appendAudioGraphNode(graph, 1, "oscillator", true, { { "volume", 1.0f }, { "waveform", 0.0f } });
   appendAudioGraphNode(graph, 2, "output", true, {});
   graph.connections.push_back({ 1, 0, 2, 0, PortType::Audio, PortType::Audio });
   compileAudioProcessPlan(graph, graph.processPlan);

   constexpr int kBlock = 1024;
   constexpr float kRate = 44100.0f;
   AudioGraphEngine engine;
   engine.prepareForBlock(kBlock, 128, 32);
   engine.queueNote({ pitch, 1.0f, true, 0.0, 0 });
   std::vector<float> left(kBlock), right(kBlock), all;
   float* outputs[2] = { left.data(), right.data() };
   for (int block = 0; block < 44; ++block)
   {
      engine.processBlock(graph, outputs, 2, kBlock, kRate);
      if (block > 0)
         all.insert(all.end(), left.begin(), left.end());
   }
   return measured_frequency(all, kRate);
}

void test_scala_tuning()
{
   printf("\n=== Scala Tuning Tests ===\n");

   NoteFrequencyTable table{};
   const auto tet = buildTuningTable("", "", table);
   TEST("Empty Scala text is 12-TET", tet.ok && tet.scaleCount == 12);
   TEST("12-TET A4 is 440 Hz", std::fabs(table[69] - 440.0f) < 0.01f);

   const auto just = buildTuningTable(kJustMajorScl, "", table);
   TEST("Known .scl parses via libs/tuning-library", just.ok && just.scaleCount == 7);
   TEST("Scala scale starts on middle C", std::fabs(table[60] - 261.6256f) < 0.01f);
   const float third = table[62] / table[60];
   const float fifth = table[64] / table[60];
   // With the default mapping each MIDI key is one scale degree: 60, 61, 62 → 1/1, 9/8, 5/4.
   TEST("Scala major third is 5/4, not 12-TET 2^(4/12)",
        std::fabs(third - 1.25f) < 1e-4f && std::fabs(third - std::pow(2.0f, 4.0f / 12.0f)) > 0.005f);
   TEST("Scala fifth is 3/2", std::fabs(fifth - 1.5f) < 1e-4f);
   TEST("Seven-degree scale repeats at the octave", std::fabs(table[67] / table[60] - 2.0f) < 1e-4f);

   const auto bad = buildTuningTable("this is not a scala file", "", table);
   TEST("Malformed .scl is rejected without aborting", !bad.ok && !bad.error.empty());
   TEST("Rejected .scl leaves 12-TET in place", std::fabs(table[69] - 440.0f) < 0.01f);

   ScaleModule scale(2);
   scale.setStringProperty("scl", kJustMajorScl);
   scale.setStringProperty("sclName", "just_major.scl");
   TEST("Scale module reports custom tuning", scale.hasCustomTuning());
   const auto names = scale.stringPropertyNames();
   TEST("Scale module serializes scl extras",
        std::find(names.begin(), names.end(), "scl") != names.end());
   scale.setStringProperty("scl", "");
   TEST("Clearing scl returns to 12-TET", !scale.hasCustomTuning() && scale.stringPropertyNames().empty());

   GraphMusicScale custom(0, kScaleModeMajor);
   buildTuningTable(kJustMajorScl, "", custom.noteFrequency);
   custom.customTuning = true;
   TEST("Custom tuning bypasses 12-TET mode quantization", quantizeMidiPitch(61, custom) == 61);

   const float tetHz = render_tuned_note(GraphMusicScale(0, kScaleModeChromatic), 62);
   const float justHz = render_tuned_note(custom, 62);
   TEST("Oscillator plays 12-TET D4 near 293.7 Hz", std::fabs(tetHz - 293.66f) < 2.0f);
   TEST("Oscillator plays Scala degree 2 near 327.0 Hz", std::fabs(justHz - 327.03f) < 2.0f);
}
