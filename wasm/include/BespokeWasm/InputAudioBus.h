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

         /** Host: the microphone capture node is running (Input header button). */
         void setCaptureActive(bool active) { mCaptureActive.store(active, std::memory_order_relaxed); }
         bool captureActive() const { return mCaptureActive.load(std::memory_order_relaxed); }
         /** Largest |sample| consumed since the last call; resets on read (UI thread). */
         float takePeak() { return mPeak.exchange(0.0f, std::memory_order_relaxed); }
         /** Folds in a peak another instance consumed (the AudioWorklet engine reads the mic there). */
         void notePeak(float peak);

      private:
         InputAudioBus() = default;

         float mBuffer[kCapacity]{};
         std::atomic<uint32_t> mWrite{ 0 };
         std::atomic<uint32_t> mRead{ 0 };
         std::atomic<bool> mCaptureActive{ false };
         std::atomic<float> mPeak{ 0.0f };
      };

   } // namespace wasm
} // namespace bespoke
