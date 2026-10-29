/**
 * BespokeSynth WASM - Script module adapter (JS livecoding, ?script=1)
 *
 * No DSP. The user's JavaScript runs in a capability-limited worker owned by
 * the TS shell; its notes arrive through ScriptNoteQueue and leave this module's
 * Note output. Control changes go straight to the canvas on the UI thread.
 * Source text lives in extras.script.
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
      struct ScriptParams
      {
         int moduleId = -1;
      };

      class ScriptModuleAdapter : public AdapterStorage<ScriptParams>
      {
      public:
         const char* typeId() const override { return "script"; }
         const char* displayName() const override { return "Script"; }
         ModuleCategory category() const override { return ModuleCategory::Other; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteSource; }

         ParamTable paramTable() const override { return {}; }
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         /** Stores the module id, which keys this module's ScriptNoteQueue. */
         void bindParams(int moduleId, const WasmStringMap& extras, void* params) const override;

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
