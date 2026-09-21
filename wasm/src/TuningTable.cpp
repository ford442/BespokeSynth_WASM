/**
 * BespokeSynth WASM - Scala tuning tables
 *
 * This translation unit is compiled with -fwasm-exceptions (see CMakeLists.txt)
 * so Tunings::TuningError from malformed user files is caught here instead of
 * aborting the module.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/TuningTable.h"
#include "BespokeWasm/Tunings.h"

namespace bespoke
{
   namespace wasm
   {
      TuningParseResult buildTuningTable(const std::string& scl,
                                         const std::string& kbm,
                                         NoteFrequencyTable& table)
      {
         TuningParseResult result;
         fillEqualTemperament(table);
         try
         {
            const Tunings::Scale scale =
            scl.empty() ? Tunings::evenTemperament12NoteScale() : Tunings::parseSCLData(scl);
            const Tunings::KeyboardMapping mapping =
            kbm.empty() ? Tunings::KeyboardMapping() : Tunings::parseKBMData(kbm);
            const Tunings::Tuning tuning(scale, mapping);
            for (int note = 0; note < 128; ++note)
               table[static_cast<size_t>(note)] = static_cast<float>(tuning.frequencyForMidiNote(note));
            result.ok = true;
            result.scaleCount = scale.count;
            result.description = scale.description;
         }
         catch (const Tunings::TuningError& error)
         {
            fillEqualTemperament(table);
            result.error = error.what();
         }
         catch (const std::exception& error)
         {
            fillEqualTemperament(table);
            result.error = error.what();
         }
         return result;
      }
   } // namespace wasm
} // namespace bespoke
