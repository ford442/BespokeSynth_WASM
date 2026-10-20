/**
 * BespokeSynth WASM - Note filter (Note processor) adapter
 *
 * A pitch-range gate: passes notes inside (or outside) [low, high]. A note-off
 * always follows its note-on, even if the range changed while the key was
 * held, so a gate edit can never leave a downstream voice hanging.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include <cstdint>

namespace bespoke
{
   namespace wasm
   {

      struct NoteFilterParams
      {
         int low = 0;
         int high = 127;
         bool passOutside = false;
      };

      struct NoteFilterRuntimeState
      {
         uint64_t passed[2]{}; // bit per pitch: its note-on went through
      };

      class NoteFilterModuleAdapter : public AdapterStorage<NoteFilterParams, NoteFilterRuntimeState>
      {
      public:
         const char* typeId() const override { return "notefilter"; }
         const char* displayName() const override { return "Note Filter"; }
         ModuleCategory category() const override { return ModuleCategory::NoteEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteProcessor; }
         bool quantizesNotesToScale() const override { return false; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processNotes(void* runtimeState,
                           const void* params,
                           const WasmNoteProcessBlock& block,
                           WasmNoteEvent* outNotes,
                           int maxNotes,
                           int& outCount) const override;

         static bool passes(const NoteFilterParams& params, int pitch);
      };

   } // namespace wasm
} // namespace bespoke
