#include "BespokeWasm/AudioAnalysis.h"
#include "BespokeWasm/DspSimd.h"

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
      // Twiddles laid out per stage so each butterfly run reads them contiguously:
      // the stage with half-size h starts at offset h - 1 (total kFftSize - 1).
      std::vector<float> gStageCos;
      std::vector<float> gStageSin;
      std::vector<float> gFftReal;
      std::vector<float> gFftImag;
      bool gTablesReady = false;
      unsigned int gCpuSpectrumCount = 0;
      unsigned int gGpuSpectrumCount = 0;

      void ensureFftTables()
      {
         if (gTablesReady)
            return;

         gHannWindow.resize(static_cast<size_t>(kFftSize));
         for (int i = 0; i < kFftSize; ++i)
            gHannWindow[static_cast<size_t>(i)] =
               0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(kFftSize - 1));

         gStageCos.resize(static_cast<size_t>(kFftSize - 1));
         gStageSin.resize(static_cast<size_t>(kFftSize - 1));
         for (int half = 1; half < kFftSize; half <<= 1)
         {
            const int step = kFftSize / (half * 2);
            for (int j = 0; j < half; ++j)
            {
               const float phase = -kTwoPi * static_cast<float>(j * step) / static_cast<float>(kFftSize);
               gStageCos[static_cast<size_t>(half - 1 + j)] = std::cos(phase);
               gStageSin[static_cast<size_t>(half - 1 + j)] = std::sin(phase);
            }
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

         float* re = real.data();
         float* im = imag.data();
         for (int half = 1; half < n; half <<= 1)
         {
            const float* twRe = gStageCos.data() + (half - 1);
            const float* twIm = gStageSin.data() + (half - 1);
            for (int i = 0; i < n; i += half * 2)
               dsp::butterflyStage(re + i, im + i, re + i + half, im + i + half, twRe, twIm, half);
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
      ++gCpuSpectrumCount;

      bins = std::min(bins, kSpectrumBins);
      for (int i = 0; i < kFftSize; ++i)
      {
         gFftReal[static_cast<size_t>(i)] = 0.0f;
         gFftImag[static_cast<size_t>(i)] = 0.0f;
      }
      copyLatest(gFftReal.data(), kFftSize);
      dsp::multiply(gFftReal.data(), gHannWindow.data(), kFftSize);

      fftInPlace(gFftReal, gFftImag);

      const int usableBins = std::min(bins, kFftSize / 2);
      dsp::magnitude(destination, gFftReal.data(), gFftImag.data(), usableBins, 0.004f, 1.0f);
      for (int bin = usableBins; bin < bins; ++bin)
         destination[bin] = 0.0f;
   }

   unsigned int cpuSpectrumCount()
   {
      return gCpuSpectrumCount;
   }

   void noteGpuSpectrumDispatch()
   {
      ++gGpuSpectrumCount;
   }

   unsigned int gpuSpectrumCount()
   {
      return gGpuSpectrumCount;
   }
}
