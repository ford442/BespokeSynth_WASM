/**
 * BespokeSynth WASM - Patch state JSON serialization
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/ModuleCanvas.h"
#include <string>

namespace bespoke
{
   namespace wasm
   {
      constexpr int kPatchSchemaVersionV1 = 1;
      constexpr int kPatchSchemaVersion = 3;

      std::string serializePatchState(const ModuleCanvas::StateSnapshot& snapshot, int viewMode);
      bool deserializePatchState(const std::string& json,
                                 ModuleCanvas::StateSnapshot& snapshot,
                                 int& viewMode,
                                 std::string& error);
      void migratePatchState(ModuleCanvas::StateSnapshot& snapshot);
   } // namespace wasm
} // namespace bespoke
