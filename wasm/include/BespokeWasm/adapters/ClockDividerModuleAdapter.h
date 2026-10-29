/**
 * BespokeSynth WASM - Clock divider (Pulse processor) adapter
 *
 * A PulseSource that consumes Pulse: the plan orders it after the sources
 * feeding it, and the engine hands it their pulses for the same block.
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

      struct ClockDividerParams
      {
         int divide = 4;
         int offset = 0;
      };

      struct ClockDividerRuntimeState
      {
         long long pulseCount = 0;
      };

      class ClockDividerModuleAdapter : public AdapterStorage<ClockDividerParams, ClockDividerRuntimeState>
      {
      public:
         const char* typeId() const override { return "clockdivider"; }
         const char* displayName() const override { return "Clock Divider"; }
         ModuleCategory category() const override { return ModuleCategory::Pulse; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::PulseSource; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void emitPulsesForBeatRange(void* runtimeState,
                                     const void* params,
                                     double beatStart,
                                     double beatEnd,
                                     WasmPulseEvent* outPulses,
                                     int maxPulses,
                                     int& outCount,
                                     const WasmPulseEvent* inputPulses,
                                     int inputPulseCount) const override;
      };

   } // namespace wasm
} // namespace bespoke
