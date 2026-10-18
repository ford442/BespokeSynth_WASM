/**
 * BespokeSynth WASM - Output sink adapter
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

      struct OutputParams
      {
         bool limit = true;
      };

      struct OutputRuntimeState
      {
         float gain = 1.0f; // limiter gain, <= 1
      };

      /**
       * Terminal sink with a built-in peak limiter (desktop parity: the desktop
       * Output clamps at its `limit`). Linked L/R, instant attack, exponential
       * release, no lookahead, so output peaks never exceed kLimiterCeiling.
       */
      class OutputModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "output"; }
         const char* displayName() const override { return "Output"; }
         ModuleCategory category() const override { return ModuleCategory::Other; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::Sink; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(OutputParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(OutputRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         static constexpr float kLimiterCeiling = 0.966f; // -0.3 dBFS
         static constexpr float kLimiterReleaseMs = 80.0f;
      };

   } // namespace wasm
} // namespace bespoke
