/**
 * BespokeSynth WASM - Output sink adapter
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

      struct OutputParams
      {
         bool limit = true;
         /** Patches saved before schema v5 load with this off, so they render as before. */
         bool dcBlock = true;
      };

      struct OutputRuntimeState
      {
         float gain = 1.0f; // limiter gain, <= 1
         float dcInput[2]{}; // DC blocker x[n-1] per channel
         float dcOutput[2]{}; // DC blocker y[n-1] per channel
      };

      /**
       * Terminal sink with a DC blocker (one-pole high-pass at kDcBlockHz) ahead of a built-in peak limiter (desktop parity: the desktop
       * Output clamps at its `limit`). Linked L/R, instant attack, exponential
       * release, no lookahead, so output peaks never exceed kLimiterCeiling.
       */
      class OutputModuleAdapter : public AdapterStorage<OutputParams, OutputRuntimeState>
      {
      public:
         const char* typeId() const override { return "output"; }
         const char* displayName() const override { return "Output"; }
         ModuleCategory category() const override { return ModuleCategory::Other; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::Sink; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         static constexpr float kLimiterCeiling = 0.966f; // -0.3 dBFS
         static constexpr float kLimiterReleaseMs = 80.0f;
         static constexpr float kDcBlockHz = 10.0f;
      };

   } // namespace wasm
} // namespace bespoke
