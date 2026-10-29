/**
 * BespokeSynth WASM - Ramper modulator adapter
 *
 * A tempo-timed ramp as a modulation source. In one-shot mode a note-on or pulse
 * ramps the output from `start` to `target` over `length` and holds there; in loop
 * mode the ramp is locked to the transport beat (a tempo-synced sawtooth) and a
 * trigger is not needed. Reference only: Source/Ramper.cpp targets a control
 * directly and is not compiled; here the ramp is a cable like the LFO's.
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

      struct RamperParams
      {
         float start = 0.0f;
         float target = 1.0f;
         int length = 4;
         int mode = 0;
      };

      struct RamperAdapterRuntimeState
      {
         /** 0..1 through the one-shot ramp; < 0 before the first trigger. */
         double progress = -1.0;
      };

      class RamperModuleAdapter : public AdapterStorage<RamperParams, RamperAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "ramper"; }
         const char* displayName() const override { return "Ramper"; }
         ModuleCategory category() const override { return ModuleCategory::Modulator; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::ModulationSource; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         /** Ramp length in beats for the `length` choice (1/16 .. 8 bars of 4/4). */
         static double lengthBeats(int choice);
      };

   } // namespace wasm
} // namespace bespoke
