/**
 * BespokeSynth WASM - Noise source adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      struct NoiseAdapterRuntimeState
      {
         uint32_t rng = 0xA5A5A5A5u;
         float pinkB0 = 0.0f;
         float pinkB1 = 0.0f;
         float pinkB2 = 0.0f;
      };

      class NoiseModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "noise"; }
         const char* displayName() const override { return "Noise"; }
         ModuleCategory category() const override { return ModuleCategory::Synth; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioSource; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;
         void fillAudioGraphNode(const std::map<std::string, float>& controls,
                                 AudioGraphNode& node) const override;

         size_t runtimeStateSize() const override { return sizeof(NoiseAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const AudioGraphNode& node,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
