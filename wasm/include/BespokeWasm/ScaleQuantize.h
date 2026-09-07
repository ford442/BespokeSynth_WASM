/**
 * BespokeSynth WASM - 12-TET scale quantization (snapshot helper, not a DSP node)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include <cmath>

namespace bespoke
{
   namespace wasm
   {
      constexpr int kScaleModeMajor = 0;
      constexpr int kScaleModeMinor = 1;
      constexpr int kScaleModeDorian = 2;
      constexpr int kScaleModeMixolydian = 3;
      constexpr int kScaleModePentatonic = 4;
      constexpr int kScaleModeBlues = 5;
      constexpr int kScaleModeChromatic = 6;

      inline int quantizeMidiPitch(int pitch, const GraphMusicScale& scale)
      {
         if (scale.mode == kScaleModeChromatic)
            return pitch;

         static constexpr int kMajor[] = { 0, 2, 4, 5, 7, 9, 11 };
         static constexpr int kMinor[] = { 0, 2, 3, 5, 7, 8, 10 };
         static constexpr int kDorian[] = { 0, 2, 3, 5, 7, 9, 10 };
         static constexpr int kMixolydian[] = { 0, 2, 4, 5, 7, 9, 10 };
         static constexpr int kPentatonic[] = { 0, 2, 4, 7, 9 };
         static constexpr int kBlues[] = { 0, 3, 5, 6, 7, 10 };

         const int* degrees = kMajor;
         int degreeCount = 7;
         switch (scale.mode)
         {
            case kScaleModeMinor:
               degrees = kMinor;
               break;
            case kScaleModeDorian:
               degrees = kDorian;
               break;
            case kScaleModeMixolydian:
               degrees = kMixolydian;
               break;
            case kScaleModePentatonic:
               degrees = kPentatonic;
               degreeCount = 5;
               break;
            case kScaleModeBlues:
               degrees = kBlues;
               degreeCount = 6;
               break;
            default:
               degrees = kMajor;
               break;
         }

         const int root = ((scale.root % 12) + 12) % 12;
         const int pc = ((pitch % 12) + 12) % 12;
         const int relative = (pc - root + 12) % 12;

         int bestDegree = degrees[0];
         int bestDist = 12;
         for (int i = 0; i < degreeCount; ++i)
         {
            const int degree = degrees[i];
            int dist = std::abs(relative - degree);
            dist = std::min(dist, 12 - dist);
            if (dist < bestDist || (dist == bestDist && degree > bestDegree))
            {
               bestDist = dist;
               bestDegree = degree;
            }
         }

         const int quantizedPc = (root + bestDegree) % 12;
         const int octaveBase = pitch - pc;
         int quantized = octaveBase + quantizedPc;
         if (quantized - pitch > 6)
            quantized -= 12;
         else if (pitch - quantized > 6)
            quantized += 12;
         if (quantized < 0)
            quantized = 0;
         if (quantized > 127)
            quantized = 127;
         return quantized;
      }
   } // namespace wasm
} // namespace bespoke
