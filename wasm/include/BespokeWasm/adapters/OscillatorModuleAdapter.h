/**
 * BespokeSynth WASM - Oscillator source adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "Oscillator.h"
#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      struct OscillatorParams
      {
         float frequency = 440.0f;
         float volume = 0.7f;
         int waveform = 0;
      };

      struct OscillatorAdapterRuntimeState
      {
         float phase = 0.0f;
         float noteFrequency = 440.0f;
         float noteVelocity = 1.0f;
         bool noteGate = true;
         bool hasReceivedNote = false;
         OscillatorType oscillatorType = kOsc_Sin;
         Oscillator oscillator{ kOsc_Sin };
      };

      class OscillatorModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "oscillator"; }
         const char* displayName() const override { return "Oscillator"; }
         ModuleCategory category() const override { return ModuleCategory::Synth; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioSource; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(OscillatorParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(OscillatorAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
