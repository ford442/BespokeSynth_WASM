#include "BespokeWasm/AudioAnalysis.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/dsp/Fft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace bespoke::wasm::AudioAnalysis
{
   namespace
   {
      std::array<float, kRingSize> ring{};
      std::atomic<unsigned int> writeIndex{ 0 };
      constexpr float kTwoPi = 6.28318530717958647692f;

      std::vector<float> gHannWindow;
      std::vector<float> gFftReal;
      std::vector<float> gFftImag;
      // Shared real FFT (PFFFT, the vocoder's too); made on the first CPU spectrum.
      std::unique_ptr<dsp::RealFft> gFft;
      unsigned int gCpuSpectrumCount = 0;
      unsigned int gGpuSpectrumCount = 0;

      void ensureFftTables()
      {
         if (gFft)
            return;

         gHannWindow.resize(static_cast<size_t>(kFftSize));
         for (int i = 0; i < kFftSize; ++i)
            gHannWindow[static_cast<size_t>(i)] =
               0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(kFftSize - 1));
         gFftReal.resize(static_cast<size_t>(kFftSize / 2));
         gFftImag.resize(static_cast<size_t>(kFftSize / 2));
         gFft = std::make_unique<dsp::RealFft>(kFftSize);
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

   void pushStereo(const float* left, const float* right, int count)
   {
      if (!left || count <= 0)
         return;
      if (!right)
      {
         pushSamples(left, count);
         return;
      }
      unsigned int index = writeIndex.load(std::memory_order_relaxed);
      for (int i = 0; i < count; ++i)
         ring[(index + static_cast<unsigned int>(i)) % kRingSize] = (left[i] + right[i]) * 0.5f;
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
      float* time = gFft->timeBuffer();
      copyLatest(time, kFftSize);
      dsp::multiply(time, gHannWindow.data(), kFftSize);
      gFft->forward();

      // Ordered real layout: [DC, Nyquist, re1, im1, ...]; bin 0 is the DC term.
      const float* spectrum = gFft->spectrumBuffer();
      gFftReal[0] = spectrum[0];
      gFftImag[0] = 0.0f;
      for (int bin = 1; bin < kFftSize / 2; ++bin)
      {
         gFftReal[static_cast<size_t>(bin)] = spectrum[2 * bin];
         gFftImag[static_cast<size_t>(bin)] = spectrum[2 * bin + 1];
      }

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
