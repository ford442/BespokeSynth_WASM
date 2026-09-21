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

#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {
      struct ScriptParams
      {
         int moduleId = -1;
      };

      class ScriptModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "script"; }
         const char* displayName() const override { return "Script"; }
         ModuleCategory category() const override { return ModuleCategory::Other; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteSource; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override { return {}; }
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(ScriptParams); }
         void fillParams(int moduleId,
                         const WasmControlMap& controls,
                         const WasmStringMap& extras,
                         void* dst) const override;

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
