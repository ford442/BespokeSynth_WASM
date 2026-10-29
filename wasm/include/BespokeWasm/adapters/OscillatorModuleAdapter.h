/**
 * BespokeSynth WASM - Oscillator source adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "Oscillator.h"
#include "BespokeWasm/AdapterStorage.h"

namespace bespoke
{
   namespace wasm
   {
      constexpr int kOscillatorVoiceCount = 8;

      struct OscillatorParams
      {
         float frequency = 440.0f;
         float volume = 0.7f;
         int waveform = 0;
      };

      struct OscillatorVoice
      {
         float phase = 0.0f;
         float frequency = 440.0f;
         float velocity = 1.0f;
         int pitch = 60;
         bool gated = false;
         uint32_t age = 0;
      };

      struct OscillatorAdapterRuntimeState
      {
         OscillatorVoice voices[kOscillatorVoiceCount]{};
         uint32_t ageCounter = 0;
         float freeRunPhase = 0.0f;
         Oscillator oscillator{ kOsc_Sin };
         bool hasReceivedNote = false;
         OscillatorType oscillatorType = kOsc_Sin;
      };

      class OscillatorModuleAdapter : public AdapterStorage<OscillatorParams, OscillatorAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "oscillator"; }
         const char* displayName() const override { return "Oscillator"; }
         ModuleCategory category() const override { return ModuleCategory::Synth; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioSource; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
