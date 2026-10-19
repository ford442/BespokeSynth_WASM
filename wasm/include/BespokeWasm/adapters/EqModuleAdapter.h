/**
 * BespokeSynth WASM - 4-band parametric EQ adapter
 *
 * Low shelf, two peaking bands, high shelf, then an output trim. Each band is a
 * Source/BiquadFilter (already in CORE_SOURCES); Source/EQ.cpp is a drawable
 * module and is not compiled. Coefficients are recomputed only when a control
 * changes, and a band at 0 dB is skipped.
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

      constexpr int kEqBands = 4;

      struct EqBandParams
      {
         float freq = 1000.0f;
         float gainDb = 0.0f;
         float q = 1.0f;
      };

      struct EqParams
      {
         EqBandParams band[kEqBands];
         float outputDb = 0.0f;
      };

      struct EqRuntimeState
      {
         BiquadFilter filter[kEqBands][2]; // [band][channel], coefficients shared per band
         EqBandParams applied[kEqBands];
         bool initialized = false;
         float lastSampleRate = 0.0f;
      };

      class EqModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "eq"; }
         const char* displayName() const override { return "EQ"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(EqParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(EqRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         static FilterType bandType(int band);
      };

   } // namespace wasm
} // namespace bespoke
