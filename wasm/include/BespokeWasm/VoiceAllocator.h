/**
 * BespokeSynth WASM - Allocation-free voice steal helper
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstdint>

namespace bespoke
{
   namespace wasm
   {
      template <typename Voice>
      Voice* findGatedVoiceByPitch(Voice* voices, int count, int pitch)
      {
         for (int i = 0; i < count; ++i)
         {
            if (voices[i].gated && voices[i].pitch == pitch)
               return &voices[i];
         }
         return nullptr;
      }

      template <typename Voice>
      Voice* allocateVoice(Voice* voices, int count, uint32_t& ageCounter, int pitch)
      {
         if (Voice* existing = findGatedVoiceByPitch(voices, count, pitch))
         {
            existing->age = ++ageCounter;
            return existing;
         }

         Voice* freeSlot = nullptr;
         Voice* oldest = &voices[0];
         for (int i = 0; i < count; ++i)
         {
            if (!voices[i].gated)
            {
               freeSlot = &voices[i];
               break;
            }
            if (voices[i].age < oldest->age)
               oldest = &voices[i];
         }

         Voice* voice = freeSlot ? freeSlot : oldest;
         voice->age = ++ageCounter;
         voice->pitch = pitch;
         return voice;
      }

      template <typename Voice>
      Voice* releaseVoice(Voice* voices, int count, int pitch)
      {
         return findGatedVoiceByPitch(voices, count, pitch);
      }
   } // namespace wasm
} // namespace bespoke
