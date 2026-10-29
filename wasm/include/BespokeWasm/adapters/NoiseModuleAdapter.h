/**
 * BespokeSynth WASM - Noise source adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include <cstdint>

namespace bespoke
{
   namespace wasm
   {

      struct NoiseParams
      {
         float volume = 0.35f;
         int color = 0;
      };

      struct NoiseAdapterRuntimeState
      {
         uint32_t rng = 0xA5A5A5A5u;
         float pinkB0 = 0.0f;
         float pinkB1 = 0.0f;
         float pinkB2 = 0.0f;
      };

      class NoiseModuleAdapter : public AdapterStorage<NoiseParams, NoiseAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "noise"; }
         const char* displayName() const override { return "Noise"; }
         ModuleCategory category() const override { return ModuleCategory::Synth; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioSource; }

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
