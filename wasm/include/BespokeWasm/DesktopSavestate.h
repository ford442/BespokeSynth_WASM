/**
 * BespokeSynth WASM - Desktop savestate (.bsk) import, layout subset
 *
 * A desktop .bsk is FileStream data: either the JSON layout string first (old files), or
 * "bskfile", the save-state revision, a PNG screenshot and then the JSON layout. Strings
 * are a uint64 length (uint32 in old 32-bit saves) and the bytes. After the layout comes
 * each module's binary state, which this import does not read: modules load with their
 * WASM defaults plus what the layout JSON carries (see the alias table in the .cpp).
 *
 * Desktop types the WASM build has map onto its adapters; the rest load as placeholders
 * that keep their type, position, name and cables, and save back unchanged.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/ModuleCanvas.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace bespoke
{
   namespace wasm
   {
      struct DesktopImportReport
      {
         int modules = 0; // layout modules read
         int placeholders = 0; // of those, types this build lacks
         int connections = 0; // cables created (including kept placeholder cables)
         int droppedTargets = 0; // targets naming no module, or no compatible input
         int saveStateRev = 0; // 0 for old files without the "bskfile" header
      };

      /** WASM type for a desktop module type, or nullptr when this build has none. */
      const char* wasmTypeForDesktopType(std::string_view desktopType);

      /** True when the bytes start like a desktop .bsk (a FileStream string, not a '{'). */
      bool looksLikeDesktopSavestate(const uint8_t* bytes, size_t length);

      /** The JSON layout string of a desktop .bsk; false with `error` on a truncated file. */
      bool extractDesktopLayoutJson(const uint8_t* bytes, size_t length, std::string& json, int& saveStateRev,
                                    std::string& error);

      /** Desktop layout JSON (the "modules" array) to a canvas snapshot. */
      bool importDesktopLayout(const std::string& json, ModuleCanvas::StateSnapshot& snapshot,
                               DesktopImportReport& report, std::string& error);

      /** .bsk bytes, or a desktop layout .json, to a canvas snapshot. */
      bool importDesktopSavestate(const uint8_t* bytes, size_t length, ModuleCanvas::StateSnapshot& snapshot,
                                  DesktopImportReport& report, std::string& error);

   } // namespace wasm
} // namespace bespoke
