/**
 * BespokeSynth WASM - Delay effect adapter implementation
 */

#include "BespokeWasm/adapters/DelayModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         // Upper bound on samples handled per vectorized span (stack scratch size).
         constexpr size_t kDelayChunk = 64;

         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }

         float* delayLine(DelayAdapterRuntimeState& state, int channel)
         {
            return state.lines[channel];
         }

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "time", .minValue = 0.01f, .maxValue = 2.0f, .defaultValue = 0.25f, .offset = offsetof(DelayParams, time) },
            { .id = 1, .name = "feedback", .minValue = 0.0f, .maxValue = 0.95f, .defaultValue = 0.35f, .offset = offsetof(DelayParams, feedback) },
            { .id = 2, .name = "mix", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.35f, .offset = offsetof(DelayParams, mix), .smoothingMs = kParamSmoothingMs },
            { .id = 3, .name = "pingpong", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .offset = offsetof(DelayParams, pingpong), .smoothingMs = kParamSmoothingMs },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };
      }

      ParamTable DelayModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> DelayModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> DelayModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> DelayModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<DelayModule>(id);
      }

      int DelayModuleAdapter::outputChannelCount(const void* paramsPtr, const WasmAudioProcessContext& context) const
      {
         const auto* params = static_cast<const DelayParams*>(paramsPtr);
         return (context.inputChannels == 2 || (params && params->pingpong > 0.0f)) ? 2 : 1;
      }

      void DelayModuleAdapter::processAudio(void* runtimeState,
                                            const void* paramsPtr,
                                            float* const* channels,
                                            const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;

         auto& state = *static_cast<DelayAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const DelayParams*>(paramsPtr);
         if (state.usedCapacity == 0)
            return;

         const size_t capacity = std::min(
         state.usedCapacity,
         static_cast<size_t>(std::max(1.0f, context.sampleRate * kMaxDelaySeconds)));

         if (state.lastSampleRate != context.sampleRate)
         {
            std::memset(delayLine(state, 0), 0, sizeof(float) * state.usedCapacity);
            std::memset(delayLine(state, 1), 0, sizeof(float) * state.usedCapacity);
            state.writeIndex = 0;
            state.lastSampleRate = context.sampleRate;
         }
         // Going mono → stereo: the right line starts as a copy of the left so echoes carry on.
         if (context.numChannels == 2 && state.lastChannels == 1)
            std::memcpy(delayLine(state, 1), delayLine(state, 0), sizeof(float) * capacity);
         state.lastChannels = context.numChannels;

         const float delaySeconds = clampFloat(params.time, 0.001f, kMaxDelaySeconds);
         const float feedback = clampFloat(params.feedback, 0.0f, 0.95f);
         const float mix = clampFloat(params.mix, 0.0f, 1.0f);
         const float delaySamples = delaySeconds * context.sampleRate;

         // The delay time is constant across the block, so the read head trails
         // the write head by a fixed offset and a fixed interpolation fraction.
         float readPos = static_cast<float>(state.writeIndex) - delaySamples;
         while (readPos < 0.0f)
            readPos += static_cast<float>(capacity);
         const float frac = readPos - std::floor(readPos);
         const size_t readStart = static_cast<size_t>(readPos) % capacity;

         // A span may only read samples written before it: readIndex + span must
         // stay behind writeIndex, so span <= ceil(delaySamples) - 1.
         const size_t maxSpan = std::min(
         kDelayChunk, static_cast<size_t>(std::max(1.0f, std::ceil(delaySamples) - 1.0f)));

         const size_t total = static_cast<size_t>(context.numSamples);
         const float pingpong = clampFloat(params.pingpong, 0.0f, 1.0f);
         if (pingpong > 0.0f && context.numChannels == 2)
         {
            // Cross-coupled lines: each line's feedback blends towards the other side, and the
            // input moves onto the left line, so at 1 the echoes alternate L, R, L...
            float* lineL = delayLine(state, 0);
            float* lineR = delayLine(state, 1);
            float* outL = channels[0];
            float* outR = channels[1];
            size_t writeIndex = state.writeIndex;
            size_t readIndex = readStart;
            for (size_t i = 0; i < total; ++i)
            {
               const size_t next = (readIndex + 1) % capacity;
               const float dL = lineL[readIndex] * (1.0f - frac) + lineL[next] * frac;
               const float dR = lineR[readIndex] * (1.0f - frac) + lineR[next] * frac;
               const float inL = outL[i];
               const float inR = outR[i];
               const float mono = 0.5f * (inL + inR);
               lineL[writeIndex] = inL * (1.0f - pingpong) + mono * pingpong + feedback * (dL * (1.0f - pingpong) + dR * pingpong);
               lineR[writeIndex] = inR * (1.0f - pingpong) + feedback * (dR * (1.0f - pingpong) + dL * pingpong);
               outL[i] = inL * (1.0f - mix) + dL * mix;
               outR[i] = inR * (1.0f - mix) + dR * mix;
               writeIndex = (writeIndex + 1) % capacity;
               readIndex = next;
            }
            state.writeIndex = writeIndex;
            return;
         }

         size_t writeEnd = state.writeIndex;
         for (int ch = 0; ch < context.numChannels; ++ch)
         {
            float* line = delayLine(state, ch);
            float* buffer = channels[ch];
            size_t writeIndex = state.writeIndex;
            size_t readIndex = readStart;
            float delayed[kDelayChunk];
            size_t pos = 0;
            while (pos < total)
            {
               // Contiguous span: no write wrap, and the read (i0..i0+span) does not wrap.
               size_t span = std::min(maxSpan, total - pos);
               span = std::min(span, capacity - writeIndex);
               span = readIndex + 1 < capacity ? std::min(span, capacity - 1 - readIndex) : 0;

               float* io = buffer + pos;
               if (span == 0)
               {
                  const float input = io[0];
                  const float d = line[readIndex] * (1.0f - frac) + line[(readIndex + 1) % capacity] * frac;
                  line[writeIndex] = input + d * feedback;
                  io[0] = input * (1.0f - mix) + d * mix;
                  span = 1;
               }
               else
               {
                  const int n = static_cast<int>(span);
                  dsp::lerpRead(delayed, line + readIndex, n, frac);
                  dsp::mix2(line + writeIndex, io, 1.0f, delayed, feedback, n);
                  dsp::mix2(io, io, 1.0f - mix, delayed, mix, n);
               }

               writeIndex = (writeIndex + span) % capacity;
               readIndex = (readIndex + span) % capacity;
               pos += span;
            }
            writeEnd = writeIndex;
         }
         state.writeIndex = writeEnd;
      }

      BESPOKE_REGISTER_MODULE(DelayModuleAdapter);

   } // namespace wasm
} // namespace bespoke
