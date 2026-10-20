/**
 * BespokeSynth WASM - Filter module adapter (BiquadFilter DSP)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BiquadFilter.h"
#include "BespokeWasm/AdapterStorage.h"

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
         BiquadFilter filter[2]; // one per channel, shared coefficients
         float lastSampleRate = 0.0f;
         int lastFilterType = -1;
      };

      class FilterModuleAdapter : public AdapterStorage<FilterParams, FilterAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "filter"; }
         const char* displayName() const override { return "Filter"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         static FilterType filterTypeFor(int filterType);
      };

   } // namespace wasm
} // namespace bespoke
