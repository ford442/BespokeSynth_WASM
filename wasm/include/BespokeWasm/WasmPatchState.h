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
      std::string serializePatchState(const ModuleCanvas::StateSnapshot& snapshot, int viewMode);
      bool deserializePatchState(const std::string& json,
                                 ModuleCanvas::StateSnapshot& snapshot,
                                 int& viewMode,
                                 std::string& error);
   } // namespace wasm
} // namespace bespoke
