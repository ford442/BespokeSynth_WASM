/**
 * BespokeSynth WASM - 12-TET scale quantization (snapshot helper, not a DSP node)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include <algorithm>
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

      /** Semitones above the root of each degree of `mode` (chromatic: all twelve). Returns the count. */
      inline int scaleModeDegrees(int mode, const int*& degrees)
      {
         static constexpr int kMajor[] = { 0, 2, 4, 5, 7, 9, 11 };
         static constexpr int kMinor[] = { 0, 2, 3, 5, 7, 8, 10 };
         static constexpr int kDorian[] = { 0, 2, 3, 5, 7, 9, 10 };
         static constexpr int kMixolydian[] = { 0, 2, 4, 5, 7, 9, 10 };
         static constexpr int kPentatonic[] = { 0, 2, 4, 7, 9 };
         static constexpr int kBlues[] = { 0, 3, 5, 6, 7, 10 };
         static constexpr int kChromatic[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

         switch (mode)
         {
            case kScaleModeMinor:
               degrees = kMinor;
               return 7;
            case kScaleModeDorian:
               degrees = kDorian;
               return 7;
            case kScaleModeMixolydian:
               degrees = kMixolydian;
               return 7;
            case kScaleModePentatonic:
               degrees = kPentatonic;
               return 5;
            case kScaleModeBlues:
               degrees = kBlues;
               return 6;
            case kScaleModeChromatic:
               degrees = kChromatic;
               return 12;
            default:
               degrees = kMajor;
               return 7;
         }
      }

      inline int quantizeMidiPitch(int pitch, const GraphMusicScale& scale)
      {
         // Scala tunings define their own degrees through the keyboard map.
         if (scale.mode == kScaleModeChromatic || scale.customTuning)
            return pitch;

         const int* degrees = nullptr;
         const int degreeCount = scaleModeDegrees(scale.mode, degrees);

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

      /**
       * `pitch` snapped to the scale, then moved `steps` scale degrees (capo "diatonic").
       * Chromatic modes and Scala tunings move by semitones.
       */
      inline int transposeByScaleDegrees(int pitch, int steps, const GraphMusicScale& scale)
      {
         if (scale.mode == kScaleModeChromatic || scale.customTuning)
            return std::max(0, std::min(127, pitch + steps));
         const int* degrees = nullptr;
         const int degreeCount = scaleModeDegrees(scale.mode, degrees);
         const int root = ((scale.root % 12) + 12) % 12;
         const int snapped = quantizeMidiPitch(pitch, scale);
         const int relative = snapped - root;
         const int octave = relative >= 0 ? relative / 12 : -((11 - relative) / 12);
         const int pc = relative - octave * 12;
         int degree = 0;
         for (int i = 0; i < degreeCount; ++i)
         {
            if (degrees[i] == pc)
               degree = i;
         }
         const int target = octave * degreeCount + degree + steps;
         const int targetOctave = target >= 0 ? target / degreeCount : -((degreeCount - 1 - target) / degreeCount);
         const int targetDegree = target - targetOctave * degreeCount;
         return std::max(0, std::min(127, root + targetOctave * 12 + degrees[targetDegree]));
      }
   } // namespace wasm
} // namespace bespoke
