/**
 * BespokeSynth WASM - 4-band parametric EQ adapter
 *
 * Low shelf, two peaking bands, high shelf, then an output trim. Each band is a
 * Source/BiquadFilter (already in CORE_SOURCES); Source/EQ.cpp is a drawable
 * module and is not compiled. Coefficients are recomputed only when a control
 * changes, and a band at 0 dB is skipped. The canvas UI draws the combined response
 * curve above the sliders (WidgetPainter::drawResponseCurve).
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
         /** Sets each band's filter type (EqModuleAdapter::bandType). */
         EqRuntimeState();

         BiquadFilter filter[kEqBands][2]; // [band][channel], coefficients shared per band
         EqBandParams applied[kEqBands];
         bool initialized = false;
         float lastSampleRate = 0.0f;
      };

      class EqModuleAdapter : public AdapterStorage<EqParams, EqRuntimeState>
      {
      public:
         const char* typeId() const override { return "eq"; }
         const char* displayName() const override { return "EQ"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         /** The shelf bands (0 and 3) run at a fixed Q. */
         void finalizeParams(void* params) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         static FilterType bandType(int band);
         /** The whole EQ's gain in dB at `freq` (bands in series plus the output trim); for the response curve. */
         static float responseDb(const EqParams& params, float freq, float sampleRate);
      };

   } // namespace wasm
} // namespace bespoke
