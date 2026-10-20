/**
 * BespokeSynth WASM - Pitch bender adapter
 *
 * Adds `bend` x `range` semitones to every note passing through, as in-graph
 * PitchBend events (NoteEventKind::PitchBend), so a voice glides without a retrigger.
 * Modulating `bend` with an LFO gives vibrato. Reference only: Source/PitchBender.cpp
 * is a drawable module and is not compiled.
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

      struct PitchBenderParams
      {
         float bend = 0.0f;
         float range = 2.0f;
      };

      struct PitchBenderExtraState
      {
         float appliedSemitones = 0.0f;
      };

      class PitchBenderModuleAdapter
      : public NoteTransformAdapter<PitchBenderModuleAdapter, PitchBenderParams, PitchBenderExtraState>
      {
      public:
         const char* typeId() const override { return "pitchbender"; }
         const char* displayName() const override { return "Pitch Bender"; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         bool transformNoteOn(PitchBenderExtraState& extra,
                              const PitchBenderParams& params,
                              const WasmNoteProcessBlock& block,
                              const WasmNoteEvent& in,
                              int& pitch,
                              float& velocity) const;
         float transformBend(const PitchBenderExtraState& extra, const PitchBenderParams& params, float inputBend) const;
         void beginBlock(PitchBenderExtraState& extra,
                         const PitchBenderParams& params,
                         const WasmNoteProcessBlock& block,
                         NoteTransformHeld& held,
                         NoteWriter& writer) const;

         static float semitones(const PitchBenderParams& params) { return params.bend * params.range; }
      };

   } // namespace wasm
} // namespace bespoke
