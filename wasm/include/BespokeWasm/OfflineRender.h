/**
 * BespokeSynth WASM - Deterministic offline graph render
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      bool renderGraphOffline(const AudioGraphSnapshot& graph,
                              double seconds,
                              int sampleRate,
                              int bitsPerSample,
                              std::vector<uint8_t>& wavOut,
                              std::string& error);

      bool renderGraphOfflinePcm(const AudioGraphSnapshot& graph,
                                 double seconds,
                                 int sampleRate,
                                 std::vector<float>& interleavedStereo,
                                 std::string& error);

   } // namespace wasm
} // namespace bespoke
