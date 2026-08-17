/**
 * BespokeSynth WASM - Filter module adapter (BiquadFilter DSP)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BiquadFilter.h"
#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      struct FilterParams
      {
         float cutoff = 1000.0f;
         float resonance = 0.5f;
         int type = 0;
      };

      struct FilterAdapterRuntimeState
      {
         BiquadFilter filter;
         float lastSampleRate = 0.0f;
         int lastFilterType = -1;
      };

      class FilterModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "filter"; }
         const char* displayName() const override { return "Filter"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(FilterParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(FilterAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;

         static FilterType filterTypeFor(int filterType);
      };

   } // namespace wasm
} // namespace bespoke
