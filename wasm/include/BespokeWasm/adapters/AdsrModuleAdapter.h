/**
 * BespokeSynth WASM - ADSR envelope adapter (lightweight WASM-safe envelope)
 *
 * Source/ADSR.cpp pulls OpenFrameworksPort/TheSynth and is not safe to link from the
 * adapter layer; this keeps the same attack/decay/sustain/release controls.
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

      struct AdsrParams
      {
         float attack = 10.0f;
         float decay = 120.0f;
         float sustain = 0.7f;
         float release = 200.0f;
      };

      struct AdsrAdapterRuntimeState
      {
         enum class Stage
         {
            Idle,
            Attack,
            Decay,
            Sustain,
            Release
         };

         Stage stage = Stage::Idle;
         float level = 0.0f;
         float stageTimeSeconds = 0.0f;
         bool hasReceivedNote = false;
      };

      class AdsrModuleAdapter : public AdapterStorage<AdsrParams, AdsrAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "adsr"; }
         const char* displayName() const override { return "ADSR"; }
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
