/**
 * BespokeSynth WASM - Transport singleton adapter
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
      struct TransportPulseRuntimeState
      {
         int lastEmittedStep = -1;
      };

      class TransportModuleAdapter : public AdapterStorage<NoAdapterStorage, TransportPulseRuntimeState>
      {
      public:
         const char* typeId() const override { return "transport"; }
         const char* displayName() const override { return "Transport"; }
         ModuleCategory category() const override { return ModuleCategory::Other; }
         bool isSingleton() const override { return true; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::PulseSource; }

         ParamTable paramTable() const override;
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
