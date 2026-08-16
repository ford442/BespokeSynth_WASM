/**
 * BespokeSynth WASM - Precompiled audio process plan compiler
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"

namespace bespoke
{
   namespace wasm
   {
      /** Compile topology, buffer slots, and processor kinds (allocates; UI thread only). */
      void compileAudioProcessPlan(const AudioGraphSnapshot& snapshot, AudioProcessPlan& plan);
   } // namespace wasm
} // namespace bespoke
