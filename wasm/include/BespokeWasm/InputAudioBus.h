/**
 * BespokeSynth WASM - Live input ring (UI / capture thread -> audio thread)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace bespoke
{
   namespace wasm
   {

      class InputAudioBus
      {
      public:
         static constexpr int kCapacity = 16384;

         static InputAudioBus& instance();

         void push(const float* frames, int count);
         void consume(float* dest, int count);

      private:
         InputAudioBus() = default;

         float mBuffer[kCapacity]{};
         std::atomic<uint32_t> mWrite{ 0 };
         std::atomic<uint32_t> mRead{ 0 };
      };

   } // namespace wasm
} // namespace bespoke
