/**
 * BespokeSynth WASM - Pulse to Note converter adapter
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
      struct PulseToNoteParams
      {
         int pitch = 60;
         float gate = 0.75f;
      };

      struct PulseToNoteRuntimeState
      {
         bool noteIsOn = false;
         int activePitch = 60;
         double noteOffBeat = -1.0;
      };

      class PulseToNoteModuleAdapter : public AdapterStorage<PulseToNoteParams, PulseToNoteRuntimeState>
      {
      public:
         const char* typeId() const override { return "pulsetonote"; }
         const char* displayName() const override { return "Pulse To Note"; }
         ModuleCategory category() const override { return ModuleCategory::NoteEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteSource; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void emitNotesForBeatRange(void* runtimeState,
                                    const void* params,
                                    double beatStart,
                                    double beatEnd,
                                    WasmNoteEvent* outNotes,
                                    int maxNotes,
                                    int& outCount,
                                    const WasmPulseEvent* pulses,
                                    int pulseCount) const override;
      };

   } // namespace wasm
} // namespace bespoke
