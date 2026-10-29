/**
 * BespokeSynth WASM - Real FFT on PFFFT (wasm/third_party/pffft)
 *
 * One fixed size per instance. Construction allocates the twiddles and aligned work
 * buffers, so make it on the UI thread (a runtime-state constructor runs in
 * prepareRuntimeStates); forward / inverse never allocate. The spectrum uses PFFFT's
 * ordered real layout: [DC, Nyquist, re1, im1, re2, im2, ... re(N/2-1), im(N/2-1)].
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

struct PFFFT_Setup;

namespace bespoke
{
   namespace wasm
   {
      namespace dsp
      {

         class RealFft
         {
         public:
            /** `size` must be 2^a * 3^b * 5^c with a >= 5 (isSupportedSize). */
            explicit RealFft(int size);
            ~RealFft();
            RealFft(const RealFft&) = delete;
            RealFft& operator=(const RealFft&) = delete;

            static bool isSupportedSize(int size);

            bool valid() const { return mSetup != nullptr; }
            int size() const { return mSize; }

            /** Aligned scratch of size() floats the caller may fill / read in place. */
            float* timeBuffer() { return mTime; }
            float* spectrumBuffer() { return mSpectrum; }

            /** timeBuffer() -> spectrumBuffer(), unscaled. */
            void forward();
            /** spectrumBuffer() -> timeBuffer(), scaled by 1/size() so inverse(forward(x)) == x. */
            void inverse();

            /** Copying variants for unaligned caller buffers (`time` and `spectrum` hold size() floats). */
            void forward(const float* time, float* spectrum);
            void inverse(const float* spectrum, float* time);

         private:
            PFFFT_Setup* mSetup = nullptr;
            float* mTime = nullptr;
            float* mSpectrum = nullptr;
            float* mWork = nullptr;
            int mSize = 0;
         };

      } // namespace dsp
   } // namespace wasm
} // namespace bespoke
