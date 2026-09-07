/**
 * BespokeSynth WASM - Pulse to Note converter adapter
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

      class PulseToNoteModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "pulsetonote"; }
         const char* displayName() const override { return "Pulse To Note"; }
         ModuleCategory category() const override { return ModuleCategory::NoteEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteSource; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(PulseToNoteParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(PulseToNoteRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
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
