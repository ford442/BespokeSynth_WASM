/**
 * BespokeSynth WASM - Real FFT on PFFFT implementation
 */

#include "BespokeWasm/dsp/Fft.h"
#include "pffft.h"
#include <cstring>
#include <initializer_list>

namespace bespoke
{
   namespace wasm
   {
      namespace dsp
      {

         bool RealFft::isSupportedSize(int size)
         {
            if (size < 32)
               return false;
            int rest = size;
            int twos = 0;
            while (rest % 2 == 0)
            {
               rest /= 2;
               ++twos;
            }
            for (int factor : { 3, 5 })
            {
               while (rest % factor == 0)
                  rest /= factor;
            }
            return rest == 1 && twos >= 5;
         }

         RealFft::RealFft(int size)
         {
            if (!isSupportedSize(size))
               return;
            mSetup = pffft_new_setup(size, PFFFT_REAL);
            if (!mSetup)
               return;
            mSize = size;
            const size_t bytes = static_cast<size_t>(size) * sizeof(float);
            mTime = static_cast<float*>(pffft_aligned_malloc(bytes));
            mSpectrum = static_cast<float*>(pffft_aligned_malloc(bytes));
            mWork = static_cast<float*>(pffft_aligned_malloc(bytes));
            if (!mTime || !mSpectrum || !mWork)
            {
               pffft_destroy_setup(mSetup);
               mSetup = nullptr;
               mSize = 0;
               return;
            }
            std::memset(mTime, 0, bytes);
            std::memset(mSpectrum, 0, bytes);
         }

         RealFft::~RealFft()
         {
            pffft_aligned_free(mTime);
            pffft_aligned_free(mSpectrum);
            pffft_aligned_free(mWork);
            if (mSetup)
               pffft_destroy_setup(mSetup);
         }

         void RealFft::forward()
         {
            if (mSetup)
               pffft_transform_ordered(mSetup, mTime, mSpectrum, mWork, PFFFT_FORWARD);
         }

         void RealFft::inverse()
         {
            if (!mSetup)
               return;
            pffft_transform_ordered(mSetup, mSpectrum, mTime, mWork, PFFFT_BACKWARD);
            const float scale = 1.0f / static_cast<float>(mSize);
            for (int i = 0; i < mSize; ++i)
               mTime[i] *= scale;
         }

         void RealFft::forward(const float* time, float* spectrum)
         {
            if (!mSetup)
               return;
            std::memcpy(mTime, time, static_cast<size_t>(mSize) * sizeof(float));
            forward();
            std::memcpy(spectrum, mSpectrum, static_cast<size_t>(mSize) * sizeof(float));
         }

         void RealFft::inverse(const float* spectrum, float* time)
         {
            if (!mSetup)
               return;
            std::memcpy(mSpectrum, spectrum, static_cast<size_t>(mSize) * sizeof(float));
            inverse();
            std::memcpy(time, mTime, static_cast<size_t>(mSize) * sizeof(float));
         }

      } // namespace dsp
   } // namespace wasm
} // namespace bespoke
