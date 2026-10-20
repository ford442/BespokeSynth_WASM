/**
 * BespokeSynth WASM - Bitcrush adapter
 *
 * Resolution and sample-rate reduction, the per-sample loop of Source/BitcrushEffect.cpp
 * (an IAudioEffect inside the desktop effect chain, not compiled here).
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

      struct BitcrushParams
      {
         float crush = 1.0f; // 1 = clean; each step drops a bit (desktop range 1..24)
         float downsample = 1.0f; // hold each sample this many frames
         float mix = 1.0f;
      };

      struct BitcrushRuntimeState
      {
         float held[2]{};
         int counter[2]{};
      };

      class BitcrushModuleAdapter : public AdapterStorage<BitcrushParams, BitcrushRuntimeState>
      {
      public:
         const char* typeId() const override { return "bitcrush"; }
         const char* displayName() const override { return "Bitcrush"; }
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
      };

   } // namespace wasm
} // namespace bespoke
