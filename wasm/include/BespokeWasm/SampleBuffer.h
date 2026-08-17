/**
 * BespokeSynth WASM - Immutable PCM sample buffer
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      struct SamplePeak
      {
         float min = 0.0f;
         float max = 0.0f;
      };

      class SampleBuffer
      {
      public:
         static constexpr int kPeakBins = 256;

         SampleBuffer(std::vector<float> mono,
                      int sampleRate,
                      std::string hash,
                      std::string name);

         const float* samples() const { return mSamples.data(); }
         int frameCount() const { return static_cast<int>(mSamples.size()); }
         int sampleRate() const { return mSampleRate; }
         const std::string& hash() const { return mHash; }
         const std::string& name() const { return mName; }
         int id() const { return mId; }
         void setId(int id) { mId = id; }

         float startNorm() const { return mStart; }
         float endNorm() const { return mEnd; }
         float loopStartNorm() const { return mLoopStart; }
         float loopEndNorm() const { return mLoopEnd; }

         const std::vector<SamplePeak>& peaks() const { return mPeaks; }

      private:
         void buildPeaks();

         std::vector<float> mSamples;
         std::vector<SamplePeak> mPeaks;
         std::string mHash;
         std::string mName;
         int mSampleRate = 44100;
         int mId = -1;
         float mStart = 0.0f;
         float mEnd = 1.0f;
         float mLoopStart = 0.0f;
         float mLoopEnd = 1.0f;
      };

      using SampleBufferPtr = std::shared_ptr<const SampleBuffer>;

      std::string sha256Hex(const uint8_t* data, size_t length);
      std::vector<float> resampleMonoCatmullRom(const float* input,
                                                int inputFrames,
                                                int inputRate,
                                                int outputRate);
      bool decodeAudioFile(const uint8_t* bytes,
                           int length,
                           std::vector<float>& monoOut,
                           int& sampleRateOut,
                           std::string& error);
      bool encodeWav(const float* interleaved,
                     int frames,
                     int channels,
                     int sampleRate,
                     int bitsPerSample,
                     std::vector<uint8_t>& outBytes,
                     std::string& error);

   } // namespace wasm
} // namespace bespoke
