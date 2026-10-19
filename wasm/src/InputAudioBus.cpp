/**
 * BespokeSynth WASM - Live input ring
 */

#include "BespokeWasm/InputAudioBus.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {
      InputAudioBus& InputAudioBus::instance()
      {
         static InputAudioBus bus;
         return bus;
      }

      void InputAudioBus::push(const float* frames, int count)
      {
         if (!frames || count <= 0)
            return;

         uint32_t write = mWrite.load(std::memory_order_relaxed);
         for (int i = 0; i < count; ++i)
         {
            const uint32_t next = (write + 1u) % static_cast<uint32_t>(kCapacity);
            const uint32_t read = mRead.load(std::memory_order_acquire);
            if (next == read)
               mRead.store((read + 1u) % static_cast<uint32_t>(kCapacity), std::memory_order_release);
            mBuffer[write] = frames[i];
            write = next;
         }
         mWrite.store(write, std::memory_order_release);
      }

      void InputAudioBus::consume(float* dest, int count)
      {
         if (!dest || count <= 0)
            return;

         uint32_t read = mRead.load(std::memory_order_relaxed);
         const uint32_t write = mWrite.load(std::memory_order_acquire);
         float peak = 0.0f;
         for (int i = 0; i < count; ++i)
         {
            if (read == write)
            {
               dest[i] = 0.0f;
               continue;
            }
            dest[i] = mBuffer[read];
            peak = std::max(peak, std::fabs(dest[i]));
            read = (read + 1u) % static_cast<uint32_t>(kCapacity);
         }
         mRead.store(read, std::memory_order_release);
         notePeak(peak);
      }

      void InputAudioBus::notePeak(float peak)
      {
         float held = mPeak.load(std::memory_order_relaxed);
         while (peak > held && !mPeak.compare_exchange_weak(held, peak, std::memory_order_relaxed))
         {
         }
      }

   } // namespace wasm
} // namespace bespoke
