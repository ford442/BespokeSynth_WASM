#pragma once

#include <array>
#include <atomic>

namespace bespoke::wasm::AudioAnalysis
{
   constexpr int kRingSize = 2048;
   constexpr int kFftSize = 512;
   constexpr int kSpectrumBins = 64;

   // Single producer (audio callback), lock-free reader (render thread).
   void pushSamples(const float* samples, int count);
   void copyLatest(float* destination, int count);
   void computeSpectrum(float* destination, int bins);
}
