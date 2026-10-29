/**
 * BespokeSynth WASM - Step sequencer adapter
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

      struct StepSequencerParams
      {
         int patternMask = 0x1111;
         int pitch = 60;
         float gate = 0.75f;
         int steps = 16;
      };

      struct StepSequencerRuntimeState
      {
         int lastEmittedStep = -1;
         bool noteIsOn = false;
         int activePitch = 60;
         double noteOffBeat = -1.0;
      };

      class StepSequencerModuleAdapter : public AdapterStorage<StepSequencerParams, StepSequencerRuntimeState>
      {
      public:
         const char* typeId() const override { return "stepsequencer"; }
         const char* displayName() const override { return "Step Sequencer"; }
         ModuleCategory category() const override { return ModuleCategory::Pulse; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteSource; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void emitNotesForBeatRange(void* runtimeState,
                                    const void* params,
                                    double beatStart,
                                    double beatEnd,
                                    WasmNoteEvent* outNotes,
                                    int maxNotes,
                                    int& outCount,
                                    const WasmPulseEvent* pulses = nullptr,
                                    int pulseCount = 0) const override;
      };

   } // namespace wasm
} // namespace bespoke
