/**
 * BespokeSynth WASM - LFO modulation adapter
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

      class LfoModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "lfo"; }
         const char* displayName() const override { return "LFO"; }
         ModuleCategory category() const override { return ModuleCategory::Modulator; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::ModulationSource; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(LfoParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(LfoAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
