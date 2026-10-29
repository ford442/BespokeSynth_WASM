/**
 * BespokeSynth WASM - LFO modulation adapter
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

      struct LfoParams
      {
         float rate = 1.0f;
         float depth = 1.0f;
         int shape = 0;
      };

      struct LfoAdapterRuntimeState
      {
         float phase = 0.0f;
         OscillatorType oscillatorType = kOsc_Sin;
         Oscillator oscillator{ kOsc_Sin };
      };

      class LfoModuleAdapter : public AdapterStorage<LfoParams, LfoAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "lfo"; }
         const char* displayName() const override { return "LFO"; }
         ModuleCategory category() const override { return ModuleCategory::Modulator; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::ModulationSource; }

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
