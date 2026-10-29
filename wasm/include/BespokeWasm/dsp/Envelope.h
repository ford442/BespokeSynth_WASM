/**
 * BespokeSynth WASM - Per-voice ADSR envelope (header-only, allocation-free)
 *
 * Linear attack, exponential decay and release, in milliseconds. Shared by the
 * polyphonic voices (PolyVoiceAdapter) and the envelope modulator. Source/ADSR.cpp
 * keeps its timeline in absolute gTime and event lists, which the block-based
 * engine has no use for; this keeps the same four controls.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace bespoke
{
   namespace wasm
   {
      namespace dsp
      {

         struct EnvelopeSettings
         {
            float attackMs = 5.0f;
            float decayMs = 200.0f;
            float sustain = 0.7f;
            float releaseMs = 300.0f;
         };

         class Envelope
         {
         public:
            enum class Stage : unsigned char
            {
               Idle,
               Attack,
               Decay,
               Sustain,
               Release
            };

            /** Below this the release counts as finished (about -80 dB). */
            static constexpr float kSilent = 1.0e-4f;

            /** Starts (or retriggers) from the current level, so a stolen voice does not click. */
            void start(float peak = 1.0f)
            {
               mPeak = std::max(0.0f, peak);
               mStage = Stage::Attack;
            }

            void stop()
            {
               if (mStage != Stage::Idle)
                  mStage = Stage::Release;
            }

            void reset()
            {
               mStage = Stage::Idle;
               mLevel = 0.0f;
            }

            Stage stage() const { return mStage; }
            bool idle() const { return mStage == Stage::Idle; }
            bool released() const { return mStage == Stage::Release || mStage == Stage::Idle; }
            float level() const { return mLevel; }

            /** Per-block coefficients; call before next() whenever the settings or rate change. */
            void prepare(const EnvelopeSettings& settings, float sampleRate)
            {
               const float rate = std::max(1.0f, sampleRate);
               mAttackStep = 1.0f / std::max(1.0f, settings.attackMs * 0.001f * rate);
               mDecayCoeff = expCoeff(settings.decayMs, rate);
               mReleaseCoeff = expCoeff(settings.releaseMs, rate);
               mSustain = std::max(0.0f, std::min(1.0f, settings.sustain));
            }

            float next()
            {
               switch (mStage)
               {
                  case Stage::Attack:
                     mLevel += mAttackStep * mPeak;
                     if (mLevel >= mPeak)
                     {
                        mLevel = mPeak;
                        mStage = Stage::Decay;
                     }
                     break;
                  case Stage::Decay:
                  {
                     const float target = mSustain * mPeak;
                     mLevel = target + (mLevel - target) * mDecayCoeff;
                     if (std::fabs(mLevel - target) < kSilent)
                     {
                        mLevel = target;
                        mStage = Stage::Sustain;
                     }
                     break;
                  }
                  case Stage::Sustain:
                     mLevel = mSustain * mPeak;
                     break;
                  case Stage::Release:
                     mLevel *= mReleaseCoeff;
                     if (mLevel < kSilent)
                     {
                        mLevel = 0.0f;
                        mStage = Stage::Idle;
                     }
                     break;
                  case Stage::Idle:
                     mLevel = 0.0f;
                     break;
               }
               return mLevel;
            }

         private:
            /** One-pole coefficient that falls to -60 dB in `ms`. */
            static float expCoeff(float ms, float rate)
            {
               const float samples = std::max(1.0f, ms * 0.001f * rate);
               return std::exp(-6.907755f / samples);
            }

            Stage mStage = Stage::Idle;
            float mLevel = 0.0f;
            float mPeak = 1.0f;
            float mSustain = 0.7f;
            float mAttackStep = 1.0f;
            float mDecayCoeff = 0.0f;
            float mReleaseCoeff = 0.0f;
         };

      } // namespace dsp
   } // namespace wasm
} // namespace bespoke
