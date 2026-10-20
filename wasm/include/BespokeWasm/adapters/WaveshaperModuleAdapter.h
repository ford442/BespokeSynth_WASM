/**
 * BespokeSynth WASM - Waveshaper / saturation adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"

namespace bespoke
{
   namespace wasm
   {

      enum WaveshaperMode
      {
         kWaveshaperSoft = 0, // x / (1 + |x|)
         kWaveshaperTanh = 1,
         kWaveshaperFold = 2, // triangle wavefolder
         kWaveshaperHard = 3, // clip at +/-1
      };

      struct WaveshaperParams
      {
         float drive = 2.0f;
         int mode = kWaveshaperTanh;
         float mix = 1.0f;
         float level = 0.7f;
      };

      class WaveshaperModuleAdapter : public AdapterStorage<WaveshaperParams>
      {
      public:
         const char* typeId() const override { return "waveshaper"; }
         const char* displayName() const override { return "Waveshaper"; }
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

         static float shape(float x, int mode);
      };

   } // namespace wasm
} // namespace bespoke
