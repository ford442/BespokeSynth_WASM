/**
 * BespokeSynth WASM - Audio graph snapshot types
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/ModuleTypes.h"
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      struct AudioGraphNode
      {
         int id = -1;
         std::string type;
         bool enabled = true;
         float frequency = 440.0f;
         float volume = 1.0f;
         int waveform = 0;
         float gain = 1.0f;
         float cutoff = 1000.0f;
         float resonance = 0.5f;
         int filterType = 0;
         float lfoRate = 1.0f;
         float lfoDepth = 1.0f;
         int lfoShape = 0;
      };

      struct AudioGraphConnection
      {
         int sourceModuleId = -1;
         int sourcePortIndex = 0;
         int destModuleId = -1;
         int destPortIndex = 0;
         PortType sourcePortType = PortType::Audio;
         PortType destPortType = PortType::Audio;
      };

      struct AudioGraphSnapshot
      {
         bool transportPlaying = false;
         float transportBPM = 120.0f;
         std::vector<AudioGraphNode> nodes;
         std::vector<AudioGraphConnection> connections;
      };

   } // namespace wasm
} // namespace bespoke
