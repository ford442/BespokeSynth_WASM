/**
 * BespokeSynth WASM - MIDI CC map matching (UI thread)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {
      struct MidiCcBinding
      {
         int channel = -1;
         int cc = 0;
         int moduleId = -1;
         std::string controlName;
         float min = 0.0f;
         float max = 1.0f;
      };

      inline float midiCcScaledValue(const MidiCcBinding& binding, float value)
      {
         const float unit = std::max(0.0f, std::min(1.0f, value));
         return binding.min + unit * (binding.max - binding.min);
      }

      template <typename Fn>
      void forEachMatchingMidiCc(const std::vector<MidiCcBinding>& map,
                                 int channel,
                                 int cc,
                                 float value,
                                 Fn&& fn)
      {
         for (const auto& binding : map)
         {
            if (binding.cc != cc || binding.moduleId < 0 || binding.controlName.empty())
               continue;
            if (binding.channel >= 0 && binding.channel != channel)
               continue;
            fn(binding.moduleId, binding.controlName, midiCcScaledValue(binding, value));
         }
      }
   } // namespace wasm
} // namespace bespoke
