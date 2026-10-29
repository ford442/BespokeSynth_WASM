/**
 * BespokeSynth WASM - Trigger timing for note- and pulse-driven modulation sources
 *
 * Envelope and ramper start on a note-on or a pulse. Pulses carry a beat time, so
 * they land on their frame; notes have no in-block time and start at frame 0.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/WasmModuleAdapter.h"
#include <cmath>

namespace bespoke
{
   namespace wasm
   {

      /** The frame of `timeBeats` inside the block, clamped to [0, numSamples - 1]. */
      inline int beatToBlockFrame(const WasmAudioProcessContext& context, double timeBeats)
      {
         const double span = context.beatEnd - context.beatStart;
         if (context.numSamples <= 0 || !(span > 0.0))
            return 0;
         const double frame = std::floor((timeBeats - context.beatStart) / span * context.numSamples);
         if (frame <= 0.0)
            return 0;
         const int last = context.numSamples - 1;
         return frame >= last ? last : static_cast<int>(frame);
      }

   } // namespace wasm
} // namespace bespoke
