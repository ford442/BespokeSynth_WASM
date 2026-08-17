/**
 * BespokeSynth WASM - Sample buffer, hash, resample, and decode
 */

#include "BespokeWasm/SampleBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#include "dr_wav.h"
#include "dr_flac.h"
#include "dr_mp3.h"

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         uint32_t rotr32(uint32_t value, uint32_t bits)
         {
            return (value >> bits) | (value << (32u - bits));
         }

         void sha256Transform(uint32_t state[8], const uint8_t block[64])
         {
            static const uint32_t k[64] = {
               0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
               0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
               0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
               0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
               0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
               0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
               0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
               0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
            };

            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
            {
               w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                      (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                      (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                      static_cast<uint32_t>(block[i * 4 + 3]);
            }
            for (int i = 16; i < 64; ++i)
            {
               const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
               const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
               w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }

            uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
            uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
            for (int i = 0; i < 64; ++i)
            {
               const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
               const uint32_t ch = (e & f) ^ ((~e) & g);
               const uint32_t temp1 = h + S1 + ch + k[i] + w[i];
               const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
               const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
               const uint32_t temp2 = S0 + maj;
               h = g;
               g = f;
               f = e;
               e = d + temp1;
               d = c;
               c = b;
               b = a;
               a = temp1 + temp2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
         }

         float catmullRom(float p0, float p1, float p2, float p3, float t)
         {
            return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                           (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
                           (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
         }

         void mixToMono(const float* interleaved, int frames, int channels, std::vector<float>& mono)
         {
            mono.resize(static_cast<size_t>(frames));
            if (channels <= 1)
            {
               std::memcpy(mono.data(), interleaved, static_cast<size_t>(frames) * sizeof(float));
               return;
            }
            const float inv = 1.0f / static_cast<float>(channels);
            for (int i = 0; i < frames; ++i)
            {
               float sum = 0.0f;
               for (int ch = 0; ch < channels; ++ch)
                  sum += interleaved[i * channels + ch];
               mono[static_cast<size_t>(i)] = sum * inv;
            }
         }
      }

      SampleBuffer::SampleBuffer(std::vector<float> mono,
                                 int sampleRate,
                                 std::string hash,
                                 std::string name)
      : mSamples(std::move(mono))
      , mHash(std::move(hash))
      , mName(std::move(name))
      , mSampleRate(sampleRate > 0 ? sampleRate : 44100)
      {
         buildPeaks();
      }

      void SampleBuffer::buildPeaks()
      {
         mPeaks.assign(static_cast<size_t>(kPeakBins), {});
         if (mSamples.empty())
            return;

         const int frames = static_cast<int>(mSamples.size());
         for (int bin = 0; bin < kPeakBins; ++bin)
         {
            const int start = (bin * frames) / kPeakBins;
            int end = ((bin + 1) * frames) / kPeakBins;
            if (end <= start)
               end = start + 1;
            float minV = 1.0f;
            float maxV = -1.0f;
            for (int i = start; i < end && i < frames; ++i)
            {
               minV = std::min(minV, mSamples[static_cast<size_t>(i)]);
               maxV = std::max(maxV, mSamples[static_cast<size_t>(i)]);
            }
            mPeaks[static_cast<size_t>(bin)] = { minV, maxV };
         }
      }

      std::string sha256Hex(const uint8_t* data, size_t length)
      {
         uint32_t state[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
         };

         uint8_t block[64];
         size_t offset = 0;
         while (offset + 64 <= length)
         {
            sha256Transform(state, data + offset);
            offset += 64;
         }

         const size_t rem = length - offset;
         std::memset(block, 0, sizeof(block));
         if (rem)
            std::memcpy(block, data + offset, rem);
         block[rem] = 0x80;

         if (rem >= 56)
         {
            sha256Transform(state, block);
            std::memset(block, 0, sizeof(block));
         }

         const uint64_t bitLen = static_cast<uint64_t>(length) * 8ull;
         for (int i = 0; i < 8; ++i)
            block[63 - i] = static_cast<uint8_t>((bitLen >> (8 * i)) & 0xff);
         sha256Transform(state, block);

         static const char* hex = "0123456789abcdef";
         std::string out(64, '0');
         for (int i = 0; i < 8; ++i)
         {
            for (int b = 0; b < 4; ++b)
            {
               const uint8_t byte = static_cast<uint8_t>((state[i] >> (24 - 8 * b)) & 0xff);
               out[static_cast<size_t>(i * 8 + b * 2)] = hex[byte >> 4];
               out[static_cast<size_t>(i * 8 + b * 2 + 1)] = hex[byte & 0xf];
            }
         }
         return out;
      }

      std::vector<float> resampleMonoCatmullRom(const float* input,
                                                int inputFrames,
                                                int inputRate,
                                                int outputRate)
      {
         std::vector<float> output;
         if (!input || inputFrames <= 0 || inputRate <= 0 || outputRate <= 0)
            return output;
         if (inputRate == outputRate)
         {
            output.assign(input, input + inputFrames);
            return output;
         }

         const double ratio = static_cast<double>(inputRate) / static_cast<double>(outputRate);
         const int outFrames = std::max(1, static_cast<int>(std::llround(inputFrames / ratio)));
         output.resize(static_cast<size_t>(outFrames));
         for (int i = 0; i < outFrames; ++i)
         {
            const double src = static_cast<double>(i) * ratio;
            const int idx = static_cast<int>(src);
            const float t = static_cast<float>(src - idx);
            const int i0 = std::max(0, idx - 1);
            const int i1 = std::min(inputFrames - 1, idx);
            const int i2 = std::min(inputFrames - 1, idx + 1);
            const int i3 = std::min(inputFrames - 1, idx + 2);
            output[static_cast<size_t>(i)] = catmullRom(input[i0], input[i1], input[i2], input[i3], t);
         }
         return output;
      }

      bool decodeAudioFile(const uint8_t* bytes,
                           int length,
                           std::vector<float>& monoOut,
                           int& sampleRateOut,
                           std::string& error)
      {
         monoOut.clear();
         sampleRateOut = 0;
         if (!bytes || length <= 0)
         {
            error = "Empty audio file";
            return false;
         }

         unsigned int channels = 0;
         unsigned int sampleRate = 0;
         drwav_uint64 frames = 0;
         float* pcm = drwav_open_memory_and_read_pcm_frames_f32(
         bytes, static_cast<size_t>(length), &channels, &sampleRate, &frames, nullptr);
         if (!pcm)
         {
            drflac* flac = drflac_open_memory(bytes, static_cast<size_t>(length), nullptr);
            if (flac)
            {
               channels = flac->channels;
               sampleRate = flac->sampleRate;
               frames = flac->totalPCMFrameCount;
               std::vector<float> interleaved(static_cast<size_t>(frames * channels));
               const drflac_uint64 got = drflac_read_pcm_frames_f32(flac, frames, interleaved.data());
               drflac_close(flac);
               if (got == 0)
               {
                  error = "FLAC decode produced no frames";
                  return false;
               }
               mixToMono(interleaved.data(), static_cast<int>(got), static_cast<int>(channels), monoOut);
               sampleRateOut = static_cast<int>(sampleRate);
               return true;
            }

            drmp3 mp3;
            if (drmp3_init_memory(&mp3, bytes, static_cast<size_t>(length), nullptr))
            {
               channels = mp3.channels;
               sampleRate = mp3.sampleRate;
               const drmp3_uint64 total = drmp3_get_pcm_frame_count(&mp3);
               std::vector<float> interleaved(static_cast<size_t>(total * channels));
               const drmp3_uint64 got = drmp3_read_pcm_frames_f32(&mp3, total, interleaved.data());
               drmp3_uninit(&mp3);
               if (got == 0)
               {
                  error = "MP3 decode produced no frames";
                  return false;
               }
               mixToMono(interleaved.data(), static_cast<int>(got), static_cast<int>(channels), monoOut);
               sampleRateOut = static_cast<int>(sampleRate);
               return true;
            }

            error = "Unrecognized audio format (expected WAV, FLAC, or MP3)";
            return false;
         }

         mixToMono(pcm, static_cast<int>(frames), static_cast<int>(channels), monoOut);
         sampleRateOut = static_cast<int>(sampleRate);
         drwav_free(pcm, nullptr);
         return !monoOut.empty();
      }

      bool encodeWav(const float* interleaved,
                     int frames,
                     int channels,
                     int sampleRate,
                     int bitsPerSample,
                     std::vector<uint8_t>& outBytes,
                     std::string& error)
      {
         outBytes.clear();
         if (!interleaved || frames <= 0 || channels <= 0 || sampleRate <= 0)
         {
            error = "Invalid PCM for WAV encode";
            return false;
         }

         drwav_data_format format;
         format.container = drwav_container_riff;
         format.channels = static_cast<drwav_uint32>(channels);
         format.sampleRate = static_cast<drwav_uint32>(sampleRate);
         format.bitsPerSample = static_cast<drwav_uint32>(bitsPerSample);
         if (bitsPerSample == 32)
            format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
         else
            format.format = DR_WAVE_FORMAT_PCM;

         void* memory = nullptr;
         size_t memorySize = 0;
         drwav wav;
         if (!drwav_init_memory_write(&wav, &memory, &memorySize, &format, nullptr))
         {
            error = "Failed to start WAV writer";
            return false;
         }

         if (bitsPerSample == 32)
         {
            drwav_write_pcm_frames(&wav, static_cast<drwav_uint64>(frames), interleaved);
         }
         else if (bitsPerSample == 24)
         {
            std::vector<uint8_t> packed(static_cast<size_t>(frames * channels * 3));
            for (int i = 0; i < frames * channels; ++i)
            {
               const float clamped = std::max(-1.0f, std::min(1.0f, interleaved[i]));
               const int32_t sample = static_cast<int32_t>(std::lround(clamped * 8388607.0f));
               packed[static_cast<size_t>(i * 3 + 0)] = static_cast<uint8_t>(sample & 0xff);
               packed[static_cast<size_t>(i * 3 + 1)] = static_cast<uint8_t>((sample >> 8) & 0xff);
               packed[static_cast<size_t>(i * 3 + 2)] = static_cast<uint8_t>((sample >> 16) & 0xff);
            }
            drwav_write_raw(&wav, packed.size(), packed.data());
         }
         else
         {
            std::vector<int16_t> pcm(static_cast<size_t>(frames * channels));
            for (int i = 0; i < frames * channels; ++i)
            {
               const float clamped = std::max(-1.0f, std::min(1.0f, interleaved[i]));
               pcm[static_cast<size_t>(i)] = static_cast<int16_t>(std::lround(clamped * 32767.0f));
            }
            drwav_write_pcm_frames(&wav, static_cast<drwav_uint64>(frames), pcm.data());
         }

         drwav_uninit(&wav);
         if (!memory || memorySize == 0)
         {
            error = "WAV writer produced no bytes";
            return false;
         }
         const auto* begin = static_cast<const uint8_t*>(memory);
         outBytes.assign(begin, begin + memorySize);
         drwav_free(memory, nullptr);
         return true;
      }

   } // namespace wasm
} // namespace bespoke
