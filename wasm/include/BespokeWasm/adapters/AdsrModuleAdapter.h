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
         std::unique_ptr<Module> createUiModule(int id) const override;
         void fillAudioGraphNode(const std::map<std::string, float>& controls,
                                 AudioGraphNode& node) const override;

         size_t runtimeStateSize() const override { return sizeof(AdsrAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const AudioGraphNode& node,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
