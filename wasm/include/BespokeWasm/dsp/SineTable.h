/**
 * BespokeSynth WASM - Table sine for per-sample oscillators (header-only)
 *
 * 2048 points with linear interpolation (about -100 dB error), indexed by a phase
 * in cycles. The FM voice calls it three times per sample per voice, where
 * std::sin would dominate the block.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <array>
#include <cmath>

namespace bespoke
{
   namespace wasm
   {
      namespace dsp
      {

         class SineTable
         {
         public:
            static constexpr int kSize = 2048;

            /** sin(2 pi * cycles), any real `cycles`. */
            static float at(float cycles)
            {
               const float wrapped = cycles - std::floor(cycles);
               const float position = wrapped * static_cast<float>(kSize);
               const int index = static_cast<int>(position);
               const float frac = position - static_cast<float>(index);
               const auto& table = instance().mTable;
               const float a = table[static_cast<size_t>(index)];
               return a + (table[static_cast<size_t>(index + 1)] - a) * frac;
            }

         private:
            SineTable()
            {
               for (int i = 0; i <= kSize; ++i)
                  mTable[static_cast<size_t>(i)] =
                  static_cast<float>(std::sin(6.283185307179586 * static_cast<double>(i) / kSize));
            }

            static const SineTable& instance()
            {
               static const SineTable table;
               return table;
            }

            std::array<float, kSize + 1> mTable{};
         };

      } // namespace dsp
   } // namespace wasm
} // namespace bespoke
