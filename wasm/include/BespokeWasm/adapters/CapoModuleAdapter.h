/**
 * BespokeSynth WASM - Capo (note transpose) adapter
 *
 * Shifts incoming notes by `capo` semitones, or by scale degrees of the graph Scale
 * when `diatonic` is on. With `retrigger`, held notes replay at the new pitch as soon
 * as the capo changes; otherwise they finish where they started. Reference only:
 * Source/Capo.cpp is a drawable module and is not compiled.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/NoteTransformAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      struct CapoParams
      {
         int capo = 0;
         bool retrigger = false;
         bool diatonic = false;
      };

      struct CapoExtraState
      {
         int appliedCapo = 0;
         bool appliedDiatonic = false;
      };

      class CapoModuleAdapter : public NoteTransformAdapter<CapoModuleAdapter, CapoParams, CapoExtraState>
      {
      public:
         const char* typeId() const override { return "capo"; }
         const char* displayName() const override { return "Capo"; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         bool transformNoteOn(CapoExtraState& extra,
                              const CapoParams& params,
                              const WasmNoteProcessBlock& block,
                              const WasmNoteEvent& in,
                              int& pitch,
                              float& velocity) const;
         void beginBlock(CapoExtraState& extra,
                         const CapoParams& params,
                         const WasmNoteProcessBlock& block,
                         NoteTransformHeld& held,
                         NoteWriter& writer) const;

         static int shifted(int pitch, const CapoParams& params, const GraphMusicScale* scale);
      };

   } // namespace wasm
} // namespace bespoke
