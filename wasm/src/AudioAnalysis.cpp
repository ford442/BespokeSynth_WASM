#include "BespokeWasm/AudioAnalysis.h"

#include <algorithm>
#include <cmath>

namespace bespoke::wasm::AudioAnalysis
{
   namespace
   {
      std::array<float, kRingSize> ring{};
      std::atomic<unsigned int> writeIndex{ 0 };
      constexpr float kTwoPi = 6.28318530717958647692f;
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
      bins = std::min(bins, kSpectrumBins);
      std::array<float, kFftSize> samples{};
      copyLatest(samples.data(), kFftSize);
      for (int bin = 0; bin < bins; ++bin)
      {
         float real = 0.0f, imaginary = 0.0f;
         for (int sample = 0; sample < kFftSize; ++sample)
         {
            const float window = 0.5f - 0.5f * std::cos(kTwoPi * sample / (kFftSize - 1));
            const float phase = kTwoPi * bin * sample / kFftSize;
            real += samples[sample] * window * std::cos(phase);
            imaginary -= samples[sample] * window * std::sin(phase);
         }
         destination[bin] = std::min(1.0f, std::sqrt(real * real + imaginary * imaginary) * 0.04f);
      }
   }
}
