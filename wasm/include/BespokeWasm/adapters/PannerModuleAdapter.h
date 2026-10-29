/**
 * BespokeSynth WASM - Mono-to-stereo panner adapter
 *
 * Its input port is declared Mono, so the process plan folds a stereo cable to (L + R) / 2
 * before it arrives (PlanChannelMix::ToMono); the output is always stereo. Same equal-power
 * law as the mixer: unity at centre, +3 dB on the near side at the extremes, silence on the far.
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

      struct PannerParams
      {
         float pan = 0.5f; // 0 = hard left, 1 = hard right
         float gain = 1.0f;
      };

      class PannerModuleAdapter : public AdapterStorage<PannerParams>
      {
      public:
         const char* typeId() const override { return "panner"; }
         const char* displayName() const override { return "Panner"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
         bool sumsAudioInputs() const override { return false; }
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override;

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
