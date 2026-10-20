/**
 * BespokeSynth WASM - Chorder (Note processor) adapter
 *
 * One Note in, a chord out. Intervals come from a preset table (or three custom
 * semitone offsets); the played key is the root. Every sounding chord tone is
 * reference-counted, so overlapping chords that share a tone release it only
 * when the last chord holding it ends. Reference only: Source/Chorder.cpp is a
 * drawable module and is not compiled.
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

      constexpr int kChorderMaxTones = 4;
      constexpr int kChorderCustomPreset = 11;

      struct ChorderParams
      {
         int chord = 0;
         int inversion = 0;
         int custom[3] = { 4, 7, 0 }; // semitones above the root; 0 = off
         float level = 1.0f; // velocity of the added tones relative to the played key
      };

      struct ChorderRuntimeState
      {
         struct Held
         {
            uint8_t count = 0;
            uint8_t pitches[kChorderMaxTones]{};
            uint8_t channel = 0;
         };
         Held held[128]{};
         uint8_t refs[128]{}; // sounding chord tones per output pitch
      };

      class ChorderModuleAdapter : public AdapterStorage<ChorderParams, ChorderRuntimeState>
      {
      public:
         const char* typeId() const override { return "chorder"; }
         const char* displayName() const override { return "Chorder"; }
         ModuleCategory category() const override { return ModuleCategory::NoteEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteProcessor; }
         // Exact intervals: input notes were already snapped to the Scale on the way in.
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

         /**
          * Semitone offsets above the played key after inversion, lowest first. `isRoot[i]`
          * marks the tone that is the played key. Returns the tone count (1..kChorderMaxTones).
          */
         static int chordTones(const ChorderParams& params, int* semitones, bool* isRoot);
      };

   } // namespace wasm
} // namespace bespoke
