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

#include "BespokeWasm/WasmModuleAdapter.h"

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

      class AdsrModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "adsr"; }
         const char* displayName() const override { return "ADSR"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(AdsrParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(AdsrAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
