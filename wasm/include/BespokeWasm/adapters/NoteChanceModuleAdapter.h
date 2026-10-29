/**
 * BespokeSynth WASM - Note chance adapter
 *
 * Passes each note-on with probability `chance`. With `deterministic`, the roll is a
 * hash of (seed, sixteenth step within a `length`-step loop, pitch), so the same
 * pattern repeats every loop; otherwise a seeded xorshift stream. Reference only:
 * Source/NoteChance.cpp is a drawable module and is not compiled.
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

      struct NoteChanceParams
      {
         float chance = 1.0f;
         bool deterministic = false;
         int length = 4;
         int seed = 0;
      };

      struct NoteChanceExtraState
      {
         uint32_t rng = 0;
         int seededWith = -1;
      };

      class NoteChanceModuleAdapter
      : public NoteTransformAdapter<NoteChanceModuleAdapter, NoteChanceParams, NoteChanceExtraState>
      {
      public:
         const char* typeId() const override { return "notechance"; }
         const char* displayName() const override { return "Note Chance"; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         bool transformNoteOn(NoteChanceExtraState& extra,
                              const NoteChanceParams& params,
                              const WasmNoteProcessBlock& block,
                              const WasmNoteEvent& in,
                              int& pitch,
                              float& velocity) const;

         /** The deterministic roll in [0, 1) for a note at `beat`. */
         static float deterministicRoll(const NoteChanceParams& params, double beat, int pitch);
      };

   } // namespace wasm
} // namespace bespoke
