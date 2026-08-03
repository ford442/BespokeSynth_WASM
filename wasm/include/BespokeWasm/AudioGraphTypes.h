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
         // ADSR (times in milliseconds)
         float attack = 10.0f;
         float decay = 120.0f;
         float sustain = 0.7f;
         float release = 200.0f;
         // Delay
         float delayTime = 0.25f;
         float delayFeedback = 0.35f;
         float delayMix = 0.35f;
         // Noise
         float noiseVolume = 0.35f;
         int noiseColor = 0;
         // Step sequencer (16th-note grid; pattern bit i = step i active)
         int patternMask = 0x1111; // steps 0,4,8,12
         int seqPitch = 60;
         float gateLength = 0.75f; // fraction of step
         int steps = 16;
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
