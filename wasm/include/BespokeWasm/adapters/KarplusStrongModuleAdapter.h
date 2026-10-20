/**
 * BespokeSynth WASM - Polyphonic Karplus-Strong (plucked string) adapter
 *
 * The desktop KarplusStrongVoice kernel (Source/KarplusStrongVoice.cpp, not compiled:
 * it reads TheScale and gTime-based ramps): an exciter (sine, noise, a pitch-blended
 * mix, or saw) shaped by a short attack/decay, fed into an interpolated delay line
 * of one period with a one-pole lowpass and `feedback` in the loop. A voice ends on
 * its own once the string has decayed below -80 dB.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/PolyVoiceAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      struct KarplusStrongParams
      {
         float vol = 1.0f;
         bool invert = false;
         float filter = 1.0f;
         float feedback = 0.98f;
         int source = 2;
         float exciterFreq = 100.0f;
         float exciterAttack = 1.0f;
         float exciterDecay = 3.0f;
         float velToVolume = 1.0f;
         float pitchTone = 0.0f;
         float release = 400.0f;
         int voices = 8;
      };

      struct KarplusStrongVoice
      {
         static constexpr int kDelaySize = 4096;
         float delay[kDelaySize]{};
         int writeIndex = 0;
         float filtered = 0.0f;
         float oscPhase = 0.0f;
         /** Exciter envelope position in samples. */
         int exciterFrames = 0;
         uint32_t noise = 0x12345678u;
         /** Consecutive quiet chunks once the exciter is done; the voice ends after a few. */
         int quietChunks = 0;
      };

      class KarplusStrongModuleAdapter
      : public PolyVoiceAdapter<KarplusStrongModuleAdapter, KarplusStrongParams, KarplusStrongVoice>
      {
      public:
         const char* typeId() const override { return "karplusstrong"; }
         const char* displayName() const override { return "Karplus-Strong"; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         static int voiceLimit(const KarplusStrongParams& params) { return params.voices; }
         static dsp::EnvelopeSettings ampEnvelope(const KarplusStrongParams& params);
         static float ampPeak(const KarplusStrongParams& params, float velocity);
         void startVoice(KarplusStrongVoice& voice,
                         const PolyVoiceSlot& slot,
                         const KarplusStrongParams& params,
                         float sampleRate) const;
         bool renderVoice(KarplusStrongVoice& voice,
                          const PolyVoiceSlot& slot,
                          const KarplusStrongParams& params,
                          float* const* out,
                          int count,
                          float sampleRate) const;
      };

   } // namespace wasm
} // namespace bespoke
