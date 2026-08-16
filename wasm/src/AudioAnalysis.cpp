#include "BespokeWasm/AudioAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bespoke::wasm::AudioAnalysis
{
   namespace
   {
      std::array<float, kRingSize> ring{};
      std::atomic<unsigned int> writeIndex{ 0 };
      constexpr float kTwoPi = 6.28318530717958647692f;

      std::vector<float> gHannWindow;
      std::vector<float> gFftCos;
      std::vector<float> gFftSin;
      std::vector<float> gFftReal;
      std::vector<float> gFftImag;
      bool gTablesReady = false;

      void ensureFftTables()
      {
         if (gTablesReady)
            return;

         gHannWindow.resize(static_cast<size_t>(kFftSize));
         for (int i = 0; i < kFftSize; ++i)
            gHannWindow[static_cast<size_t>(i)] =
               0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(kFftSize - 1));

         gFftCos.resize(static_cast<size_t>(kFftSize / 2));
         gFftSin.resize(static_cast<size_t>(kFftSize / 2));
         for (int i = 0; i < kFftSize / 2; ++i)
         {
            const float phase = -kTwoPi * static_cast<float>(i) / static_cast<float>(kFftSize);
            gFftCos[static_cast<size_t>(i)] = std::cos(phase);
            gFftSin[static_cast<size_t>(i)] = std::sin(phase);
         }

         gFftReal.resize(static_cast<size_t>(kFftSize));
         gFftImag.resize(static_cast<size_t>(kFftSize));
         gTablesReady = true;
      }

      void fftInPlace(std::vector<float>& real, std::vector<float>& imag)
      {
         const int n = kFftSize;
         for (int i = 1, j = 0; i < n; ++i)
         {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
               j ^= bit;
            j ^= bit;
            if (i < j)
            {
               std::swap(real[static_cast<size_t>(i)], real[static_cast<size_t>(j)]);
               std::swap(imag[static_cast<size_t>(i)], imag[static_cast<size_t>(j)]);
            }
         }

         for (int len = 2; len <= n; len <<= 1)
         {
            const int half = len / 2;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
               for (int j = 0; j < half; ++j)
               {
                  const int twiddle = j * step;
                  const float cosv = gFftCos[static_cast<size_t>(twiddle)];
                  const float sinv = gFftSin[static_cast<size_t>(twiddle)];
                  const int even = i + j;
                  const int odd = i + j + half;
                  const float tre = real[static_cast<size_t>(odd)] * cosv - imag[static_cast<size_t>(odd)] * sinv;
                  const float tim = real[static_cast<size_t>(odd)] * sinv + imag[static_cast<size_t>(odd)] * cosv;
                  real[static_cast<size_t>(odd)] = real[static_cast<size_t>(even)] - tre;
                  imag[static_cast<size_t>(odd)] = imag[static_cast<size_t>(even)] - tim;
                  real[static_cast<size_t>(even)] += tre;
                  imag[static_cast<size_t>(even)] += tim;
               }
            }
         }
      }
   }

   void pushSamples(const float* samples, int count)
   {
      if (!samples || count <= 0)
         return;
      unsigned int index = writeIndex.load(std::memory_order_relaxed);
      for (int i = 0; i < count; ++i)
         ring[(index + static_cast<unsigned int>(i)) % kRingSize] = samples[i];
      writeIndex.store(index + static_cast<unsigned int>(count), std::memory_order_release);
   }

   void copyLatest(float* destination, int count)
   {
      if (!destination || count <= 0)
         return;
      count = std::min(count, kRingSize);
      const unsigned int end = writeIndex.load(std::memory_order_acquire);
      const unsigned int start = end - static_cast<unsigned int>(count);
      for (int i = 0; i < count; ++i)
         destination[i] = ring[(start + static_cast<unsigned int>(i)) % kRingSize];
   }

   void computeSpectrum(float* destination, int bins)
   {
      if (!destination || bins <= 0)
         return;
      ensureFftTables();

      bins = std::min(bins, kSpectrumBins);
      for (int i = 0; i < kFftSize; ++i)
      {
         gFftReal[static_cast<size_t>(i)] = 0.0f;
         gFftImag[static_cast<size_t>(i)] = 0.0f;
      }
      copyLatest(gFftReal.data(), kFftSize);
      for (int i = 0; i < kFftSize; ++i)
         gFftReal[static_cast<size_t>(i)] *= gHannWindow[static_cast<size_t>(i)];

      fftInPlace(gFftReal, gFftImag);

      const int usableBins = std::min(bins, kFftSize / 2);
      for (int bin = 0; bin < usableBins; ++bin)
      {
         const float real = gFftReal[static_cast<size_t>(bin)];
         const float imag = gFftImag[static_cast<size_t>(bin)];
         destination[bin] = std::min(1.0f, std::sqrt(real * real + imag * imag) * 0.004f);
      }
      for (int bin = usableBins; bin < bins; ++bin)
         destination[bin] = 0.0f;
   }
}
