/**
 * BespokeSynth WASM - Distortion adapter
 *
 * The seven waveshapers of Source/DistortionEffect.cpp (clean, warm, dirty, soft,
 * asym, fold, grungy) with its clip / preamp / fuzz controls and the optional 10 Hz
 * input high-pass. The desktop effect lives in the effect chain and is not compiled;
 * its DC remover is a Source/BiquadFilter (already in the build).
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

      enum class DistortionType : int
      {
         Clean = 0,
         Warm,
         Dirty,
         Soft,
         Asymmetric,
         Fold,
         Grungy,
      };

      struct DistortionParams
      {
         int type = 0;
         float clip = 1.0f; // lower = more extreme (gain 1 / clip^3)
         float preamp = 1.0f;
         float fuzz = 0.0f; // pushes the input off-center by the tracked peak
         bool centerInput = false;
         float level = 1.0f;
      };

      struct DistortionRuntimeState
      {
         DistortionRuntimeState();

         BiquadFilter dcRemover[2];
         float peak[2]{};
         float lastSampleRate = 0.0f;
      };

      class DistortionModuleAdapter : public AdapterStorage<DistortionParams, DistortionRuntimeState>
      {
      public:
         const char* typeId() const override { return "distortion"; }
         const char* displayName() const override { return "Distortion"; }
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

         /** One sample through `type`: `x` already includes fuzz, preamp and gain. Returns before / gain. */
         static float shape(DistortionType type, float x);
      };

   } // namespace wasm
} // namespace bespoke
