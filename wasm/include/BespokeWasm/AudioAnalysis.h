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
   /** Pushes the mid (L + R) / 2 collapse; the scope and both FFT paths read this one ring. */
   void pushStereo(const float* left, const float* right, int count);
   void copyLatest(float* destination, int count);
   void computeSpectrum(float* destination, int bins);

   // Diagnostics for the live analyzer (render thread only). The WebGPU backend
   // runs the FFT in analyzer_fft.wgsl, so cpuSpectrumCount() stays flat there.
   unsigned int cpuSpectrumCount();
   void noteGpuSpectrumDispatch();
   unsigned int gpuSpectrumCount();
}
