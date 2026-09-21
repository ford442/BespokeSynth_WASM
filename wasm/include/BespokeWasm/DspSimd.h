/**
 * BespokeSynth WASM - Block DSP kernels with optional WASM SIMD128.
 *
 * Built with BESPOKE_WASM_SIMD=1 (and -msimd128) these use wasm_simd128.h;
 * otherwise, or on a non-wasm host, the scalar loops below are the reference.
 * Every kernel handles any n >= 0 (vector body + scalar tail) and tolerates
 * dst aliasing an input at the same index.
 */

#pragma once

#include <cmath>

#if defined(BESPOKE_WASM_SIMD) && defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define BESPOKE_DSP_SIMD128 1
#else
#define BESPOKE_DSP_SIMD128 0
#endif

namespace bespoke::wasm::dsp
{
   constexpr bool kSimdEnabled = BESPOKE_DSP_SIMD128 != 0;

   /** buf[i] *= gain */
   inline void scale(float* buf, int n, float gain)
   {
      int i = 0;
#if BESPOKE_DSP_SIMD128
      const v128_t g = wasm_f32x4_splat(gain);
      for (; i + 4 <= n; i += 4)
         wasm_v128_store(buf + i, wasm_f32x4_mul(wasm_v128_load(buf + i), g));
#endif
      for (; i < n; ++i)
         buf[i] *= gain;
   }

   /** dst[i] += src[i] * gain */
   inline void accumulateScaled(float* dst, const float* src, int n, float gain)
   {
      int i = 0;
#if BESPOKE_DSP_SIMD128
      const v128_t g = wasm_f32x4_splat(gain);
      for (; i + 4 <= n; i += 4)
      {
         const v128_t acc = wasm_f32x4_add(wasm_v128_load(dst + i), wasm_f32x4_mul(wasm_v128_load(src + i), g));
         wasm_v128_store(dst + i, acc);
      }
#endif
      for (; i < n; ++i)
         dst[i] += src[i] * gain;
   }

   /** dst[i] = a[i] * gainA + b[i] * gainB */
   inline void mix2(float* dst, const float* a, float gainA, const float* b, float gainB, int n)
   {
      int i = 0;
#if BESPOKE_DSP_SIMD128
      const v128_t ga = wasm_f32x4_splat(gainA);
      const v128_t gb = wasm_f32x4_splat(gainB);
      for (; i + 4 <= n; i += 4)
      {
         const v128_t out = wasm_f32x4_add(wasm_f32x4_mul(wasm_v128_load(a + i), ga),
                                           wasm_f32x4_mul(wasm_v128_load(b + i), gb));
         wasm_v128_store(dst + i, out);
      }
#endif
      for (; i < n; ++i)
         dst[i] = a[i] * gainA + b[i] * gainB;
   }

   /** dst[i] *= src[i] */
   inline void multiply(float* dst, const float* src, int n)
   {
      int i = 0;
#if BESPOKE_DSP_SIMD128
      for (; i + 4 <= n; i += 4)
         wasm_v128_store(dst + i, wasm_f32x4_mul(wasm_v128_load(dst + i), wasm_v128_load(src + i)));
#endif
      for (; i < n; ++i)
         dst[i] *= src[i];
   }

   /**
    * Fractional read of a contiguous span: dst[i] = src[i] * (1 - frac) + src[i + 1] * frac.
    * Reads src[0 .. n] inclusive.
    */
   inline void lerpRead(float* dst, const float* src, int n, float frac)
   {
      const float keep = 1.0f - frac;
      int i = 0;
#if BESPOKE_DSP_SIMD128
      const v128_t k = wasm_f32x4_splat(keep);
      const v128_t f = wasm_f32x4_splat(frac);
      for (; i + 4 <= n; i += 4)
      {
         const v128_t out = wasm_f32x4_add(wasm_f32x4_mul(wasm_v128_load(src + i), k),
                                           wasm_f32x4_mul(wasm_v128_load(src + i + 1), f));
         wasm_v128_store(dst + i, out);
      }
#endif
      for (; i < n; ++i)
         dst[i] = src[i] * keep + src[i + 1] * frac;
   }

   /**
    * One radix-2 DIT stage over contiguous halves:
    *   t = odd * w;  odd = even - t;  even += t
    * for k in [0, half). twRe/twIm hold the stage's twiddles contiguously.
    */
   inline void butterflyStage(float* evenRe,
                              float* evenIm,
                              float* oddRe,
                              float* oddIm,
                              const float* twRe,
                              const float* twIm,
                              int half)
   {
      int k = 0;
#if BESPOKE_DSP_SIMD128
      for (; k + 4 <= half; k += 4)
      {
         const v128_t wr = wasm_v128_load(twRe + k);
         const v128_t wi = wasm_v128_load(twIm + k);
         const v128_t orr = wasm_v128_load(oddRe + k);
         const v128_t oi = wasm_v128_load(oddIm + k);
         const v128_t er = wasm_v128_load(evenRe + k);
         const v128_t ei = wasm_v128_load(evenIm + k);
         const v128_t tre = wasm_f32x4_sub(wasm_f32x4_mul(orr, wr), wasm_f32x4_mul(oi, wi));
         const v128_t tim = wasm_f32x4_add(wasm_f32x4_mul(orr, wi), wasm_f32x4_mul(oi, wr));
         wasm_v128_store(oddRe + k, wasm_f32x4_sub(er, tre));
         wasm_v128_store(oddIm + k, wasm_f32x4_sub(ei, tim));
         wasm_v128_store(evenRe + k, wasm_f32x4_add(er, tre));
         wasm_v128_store(evenIm + k, wasm_f32x4_add(ei, tim));
      }
#endif
      for (; k < half; ++k)
      {
         const float tre = oddRe[k] * twRe[k] - oddIm[k] * twIm[k];
         const float tim = oddRe[k] * twIm[k] + oddIm[k] * twRe[k];
         oddRe[k] = evenRe[k] - tre;
         oddIm[k] = evenIm[k] - tim;
         evenRe[k] += tre;
         evenIm[k] += tim;
      }
   }

   /** out[i] = min(sqrt(re[i]^2 + im[i]^2) * gain, ceiling) */
   inline void magnitude(float* out, const float* re, const float* im, int n, float gain, float ceiling)
   {
      int i = 0;
#if BESPOKE_DSP_SIMD128
      const v128_t g = wasm_f32x4_splat(gain);
      const v128_t c = wasm_f32x4_splat(ceiling);
      for (; i + 4 <= n; i += 4)
      {
         const v128_t r = wasm_v128_load(re + i);
         const v128_t m = wasm_v128_load(im + i);
         const v128_t mag = wasm_f32x4_sqrt(wasm_f32x4_add(wasm_f32x4_mul(r, r), wasm_f32x4_mul(m, m)));
         wasm_v128_store(out + i, wasm_f32x4_pmin(wasm_f32x4_mul(mag, g), c));
      }
#endif
      for (; i < n; ++i)
      {
         const float v = std::sqrt(re[i] * re[i] + im[i] * im[i]) * gain;
         out[i] = v < ceiling ? v : ceiling;
      }
   }
}
