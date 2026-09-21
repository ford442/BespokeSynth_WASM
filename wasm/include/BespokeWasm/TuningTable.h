/**
 * BespokeSynth WASM - Scala (.scl / .kbm) tuning tables for the audio graph
 *
 * The Scale module owns the .scl/.kbm text (patch extras); the canvas parses it
 * on the UI thread into a fixed 128-entry MIDI-note → Hz table that rides in the
 * published AudioGraphSnapshot. Audio adapters only ever read the table.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <array>
#include <cmath>
#include <string>

namespace bespoke
{
   namespace wasm
   {
      using NoteFrequencyTable = std::array<float, 128>;

      inline float equalTemperamentFrequency(int pitch)
      {
         return 440.0f * std::pow(2.0f, (static_cast<float>(pitch) - 69.0f) / 12.0f);
      }

      inline void fillEqualTemperament(NoteFrequencyTable& table)
      {
         for (int note = 0; note < 128; ++note)
            table[static_cast<size_t>(note)] = equalTemperamentFrequency(note);
      }

      /** Hz for `pitch`, clamped into the table; 12-TET when `table` is null. */
      inline float noteFrequency(const float* table, int pitch)
      {
         if (!table)
            return equalTemperamentFrequency(pitch);
         const int index = pitch < 0 ? 0 : (pitch > 127 ? 127 : pitch);
         return table[index];
      }

      struct TuningParseResult
      {
         bool ok = false;
         /** Degrees per period of the parsed scale (12 for 12-TET). */
         int scaleCount = 12;
         std::string description;
         std::string error;
      };

      /**
       * Build a MIDI-note → Hz table from Scala text. Empty `scl` means 12-TET;
       * empty `kbm` maps scale degree 0 to MIDI 60 at 261.6256 Hz (library
       * default). Never throws; on error `table` holds 12-TET and ok == false.
       */
      TuningParseResult buildTuningTable(const std::string& scl,
                                         const std::string& kbm,
                                         NoteFrequencyTable& table);
   } // namespace wasm
} // namespace bespoke
