/**
 * BespokeSynth WASM - Polyphonic FM synth adapter
 *
 * The desktop FMVoice kernel (Source/FMVoice.cpp, not compiled: it reads TheScale,
 * gTime and ADSR event lists): a carrier frequency-modulated by a modulator at
 * `harmratio` x `tweak` of the pitch, which is itself modulated by a second one at
 * `harmratio2` x `tweak2`. Index `mod` follows its own envelope. Voices come from
 * PolyVoiceAdapter (up to 8, oldest-first stealing).
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

      struct FmSynthParams
      {
         float vol = 1.0f;
         int harmRatio = 5;
         float tweak = 1.0f;
         float mod = 2.0f;
         int harmRatio2 = 5;
         float tweak2 = 1.0f;
         float mod2 = 0.0f;
         float attack = 5.0f;
         float decay = 300.0f;
         float sustain = 0.6f;
         float release = 300.0f;
         float modAttack = 1.0f;
         float modDecay = 400.0f;
         float modSustain = 0.3f;
         int voices = 8;
      };

      struct FmVoice
      {
         float carrierPhase = 0.0f;
         float modPhase = 0.0f;
         float modPhase2 = 0.0f;
         dsp::Envelope modEnvelope;
      };

      class FmSynthModuleAdapter : public PolyVoiceAdapter<FmSynthModuleAdapter, FmSynthParams, FmVoice>
      {
      public:
         const char* typeId() const override { return "fmsynth"; }
         const char* displayName() const override { return "FM Synth"; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         static int voiceLimit(const FmSynthParams& params) { return params.voices; }
         static dsp::EnvelopeSettings ampEnvelope(const FmSynthParams& params);
         void startVoice(FmVoice& voice, const PolyVoiceSlot& slot, const FmSynthParams& params, float sampleRate) const;
         void releaseVoice(FmVoice& voice, const PolyVoiceSlot& slot, const FmSynthParams& params) const;
         bool renderVoice(FmVoice& voice,
                          const PolyVoiceSlot& slot,
                          const FmSynthParams& params,
                          float* const* out,
                          int count,
                          float sampleRate) const;

         /** The harmonic ratio of a `harmratio` choice (.125 .. 16, as the desktop dropdown). */
         static float harmonicRatio(int choice);
      };

   } // namespace wasm
} // namespace bespoke
