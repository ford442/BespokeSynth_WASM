/**
 * BespokeSynth WASM - Transport singleton adapter
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
      struct TransportPulseRuntimeState
      {
         int lastEmittedStep = -1;
      };

      class TransportModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "transport"; }
         const char* displayName() const override { return "Transport"; }
         ModuleCategory category() const override { return ModuleCategory::Other; }
         bool isSingleton() const override { return true; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::PulseSource; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t runtimeStateSize() const override { return sizeof(TransportPulseRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void emitPulsesForBeatRange(void* runtimeState,
                                     const void* params,
                                     double beatStart,
                                     double beatEnd,
                                     WasmPulseEvent* outPulses,
                                     int maxPulses,
                                     int& outCount) const override;
      };

   } // namespace wasm
} // namespace bespoke
