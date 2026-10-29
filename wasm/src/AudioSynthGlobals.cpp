/**
 * BespokeSynth WASM - Desktop DSP globals for the AudioWorklet engine module
 *
 * BiquadFilter and Oscillator (../Source) reference a few SynthGlobals /
 * OpenFrameworksPort symbols. The UI module links the full desktop files; this
 * module cannot: SynthGlobals.cpp seeds its RNG from std::random_device, which
 * needs crypto.getRandomValues, and AudioWorkletGlobalScope has no `crypto`.
 * Same behavior otherwise. Adapters set every filter's rate explicitly, so
 * gSampleRate is only the constructors' starting value.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include <cassert>
#include <cmath>
#include <cstdint>

int gSampleRate = 48000;
int gBufferSize = 128;
double gSampleRateMs = 48.0;
double gInvSampleRateMs = 1.0 / 48.0;
double gBufferSizeMs = 128.0 / 48.0;
double gNyquistLimit = 24000.0;

namespace
{
   uint32_t gRandomState = 0x9E3779B9u;

   float random01()
   {
      // xorshift32: deterministic, allocation-free, enough for ofRandom's callers.
      gRandomState ^= gRandomState << 13;
      gRandomState ^= gRandomState >> 17;
      gRandomState ^= gRandomState << 5;
      return static_cast<float>(gRandomState >> 8) / 16777216.0f;
   }
}

float Bias(float value, float bias)
{
   assert(bias >= 0 && bias <= 1);
   const float kLog25 = std::log(25.0f);
   bias = .2f * std::exp(kLog25 * bias);
   return std::pow(value, bias);
}

float ofClamp(float val, float a, float b)
{
   if (val < a)
      return a;
   if (val > b)
      return b;
   return val;
}

float ofRandom(float max)
{
   return max * random01();
}

float ofRandom(float x, float y)
{
   if (x == y)
      return x;
   const float high = x > y ? x : y;
   const float low = x > y ? y : x;
   return low + (high - low) * random01();
}
