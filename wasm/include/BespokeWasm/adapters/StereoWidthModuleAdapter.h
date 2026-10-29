/**
 * BespokeSynth WASM - Stereo width adapter
 *
 * Mid/side width (0 = mono, 1 = unchanged, 2 = twice the side signal) plus a Haas
 * delay on the right channel, which is what widens a mono source (desktop Panner's
 * "widen"). Always stereo out.
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

      struct StereoWidthParams
      {
         float width = 1.0f;
         float haasMs = 0.0f;
         float level = 1.0f;
      };

      struct StereoWidthRuntimeState
      {
         static constexpr int kHaasCapacity = 2048; // > 30 ms at 48 kHz
         int writeIndex = 0;
         float haas[kHaasCapacity]{};
      };

      class StereoWidthModuleAdapter : public AdapterStorage<StereoWidthParams, StereoWidthRuntimeState>
      {
      public:
         const char* typeId() const override { return "stereowidth"; }
         const char* displayName() const override { return "Stereo Width"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override;

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         static constexpr float kMaxHaasMs = 30.0f;
      };

   } // namespace wasm
} // namespace bespoke
