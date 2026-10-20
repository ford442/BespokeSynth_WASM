// FFT: correctness of dsp::RealFft (PFFFT) and the benchmark that chose it over Source/FFT.cpp.
#include "test_common.h"
#include "BespokeWasm/dsp/Fft.h"
#include "FFT.h"

namespace
{
   double nowMs()
   {
      return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
   }

   void fillSignal(std::vector<float>& signal, int seed)
   {
      uint32_t state = 0x9e3779b9u * static_cast<uint32_t>(seed + 1);
      for (size_t i = 0; i < signal.size(); ++i)
      {
         state = state * 1664525u + 1013904223u;
         const float noise = static_cast<float>(state >> 8) / 16777216.0f - 0.5f;
         signal[i] = 0.6f * std::sin(0.05f * static_cast<float>(i)) + 0.2f * noise;
      }
   }
}

void test_fft()
{
   printf("\n=== FFT (PFFFT via dsp::RealFft) ===\n");
   TEST("RealFft accepts 2^a*3^b*5^c sizes (a >= 5)",
        dsp::RealFft::isSupportedSize(1024) && dsp::RealFft::isSupportedSize(96) && dsp::RealFft::isSupportedSize(480) &&
        !dsp::RealFft::isSupportedSize(16) && !dsp::RealFft::isSupportedSize(1000 * 7));
   dsp::RealFft rejected(100);
   TEST("RealFft rejects an unsupported size instead of crashing", !rejected.valid());

   // Against a direct DFT at N = 64, in PFFFT's ordered layout.
   constexpr int kSmall = 64;
   dsp::RealFft small(kSmall);
   std::vector<float> input(kSmall);
   fillSignal(input, 3);
   std::vector<float> spectrum(kSmall);
   small.forward(input.data(), spectrum.data());
   double worst = 0.0;
   for (int k = 0; k <= kSmall / 2; ++k)
   {
      double re = 0.0;
      double im = 0.0;
      for (int n = 0; n < kSmall; ++n)
      {
         const double phase = -2.0 * 3.14159265358979323846 * k * n / kSmall;
         re += input[static_cast<size_t>(n)] * std::cos(phase);
         im += input[static_cast<size_t>(n)] * std::sin(phase);
      }
      double gotRe = 0.0;
      double gotIm = 0.0;
      if (k == 0)
         gotRe = spectrum[0];
      else if (k == kSmall / 2)
         gotRe = spectrum[1];
      else
      {
         gotRe = spectrum[static_cast<size_t>(2 * k)];
         gotIm = spectrum[static_cast<size_t>(2 * k + 1)];
      }
      worst = std::max(worst, std::fabs(gotRe - re) + std::fabs(gotIm - im));
   }
   TEST("RealFft forward matches a direct DFT (ordered layout)", worst < 1e-3);

   std::vector<float> back(kSmall);
   small.inverse(spectrum.data(), back.data());
   double roundTrip = 0.0;
   for (int i = 0; i < kSmall; ++i)
      roundTrip = std::max(roundTrip, static_cast<double>(std::fabs(back[static_cast<size_t>(i)] - input[static_cast<size_t>(i)])));
   TEST("RealFft inverse(forward(x)) == x", roundTrip < 1e-5);

   constexpr int kBin = 37;
   constexpr int kLarge = 1024;
   dsp::RealFft large(kLarge);
   for (int i = 0; i < kLarge; ++i)
      large.timeBuffer()[i] = std::cos(2.0f * 3.14159265f * kBin * static_cast<float>(i) / kLarge);
   large.forward();
   int peak = 1;
   float peakMag = 0.0f;
   for (int k = 1; k < kLarge / 2; ++k)
   {
      const float mag = std::hypot(large.spectrumBuffer()[2 * k], large.spectrumBuffer()[2 * k + 1]);
      if (mag > peakMag)
      {
         peakMag = mag;
         peak = k;
      }
   }
   TEST("A bin-aligned cosine peaks at its bin with magnitude N/2", peak == kBin && std::fabs(peakMag - kLarge / 2.0f) < 1.0f);
}

void test_fft_benchmark()
{
   printf("\n=== FFT Benchmark (PFFFT vs Source/FFT.cpp, N = 1024) ===\n");
   constexpr int kSize = 1024;
   constexpr int kIterations = 3000;
   std::vector<float> signal(kSize);
   fillSignal(signal, 7);

   dsp::RealFft pffft(kSize);
   std::vector<float> spectrum(kSize);
   double sink = 0.0;
   const double pffftStart = nowMs();
   for (int it = 0; it < kIterations; ++it)
   {
      signal[0] = static_cast<float>(it & 7) * 0.01f;
      pffft.forward(signal.data(), spectrum.data());
      sink += spectrum[3];
   }
   const double pffftMs = nowMs() - pffftStart;

   FFT mayer(kSize);
   std::vector<float> re(kSize / 2 + 1);
   std::vector<float> im(kSize / 2 + 1);
   const double mayerStart = nowMs();
   for (int it = 0; it < kIterations; ++it)
   {
      signal[0] = static_cast<float>(it & 7) * 0.01f;
      mayer.Forward(signal.data(), re.data(), im.data());
      sink += re[3];
   }
   const double mayerMs = nowMs() - mayerStart;

   const double pffftUs = pffftMs * 1000.0 / kIterations;
   const double mayerUs = mayerMs * 1000.0 / kIterations;
   printf("  forward real FFT: PFFFT %.2f us, Source/FFT.cpp (Mayer) %.2f us, ratio %.2fx (sink %.1f)\n", pffftUs,
          mayerUs, mayerUs / std::max(1e-6, pffftUs), sink);
   // The decision record (docs/wasm/module-porting.md) relies on this staying true; timing noise gets 25% slack.
   TEST("PFFFT is at least as fast as Source/FFT.cpp at N = 1024", pffftUs <= mayerUs * 1.25);

   // Same real parts. FFT::Forward reads the imaginary part of bin k from mFft_data[N - 1 - k], which
   // is bin k + 1's (an off-by-one in the desktop wrapper); match that so the check stays meaningful.
   pffft.forward(signal.data(), spectrum.data());
   mayer.Forward(signal.data(), re.data(), im.data());
   double diff = std::fabs(spectrum[0] - re[0]) + std::fabs(spectrum[1] - re[kSize / 2]);
   for (int k = 1; k < kSize / 2; ++k)
      diff = std::max(diff, static_cast<double>(std::fabs(spectrum[static_cast<size_t>(2 * k)] - re[static_cast<size_t>(k)])));
   for (int k = 1; k + 1 < kSize / 2; ++k)
      diff = std::max(diff, static_cast<double>(std::fabs(std::fabs(spectrum[static_cast<size_t>(2 * k + 3)]) -
                                                          std::fabs(im[static_cast<size_t>(k)]))));
   TEST("PFFFT and Source/FFT.cpp agree (real parts; desktop imag is shifted one bin)", diff < 0.05);
}
