/**
 * BespokeSynth WASM - Tempo-synced ping-pong delay adapter implementation
 */

#include "BespokeWasm/adapters/PingPongDelayModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "interval", .minValue = 0.0f, .maxValue = static_cast<float>(PingPongDelayModuleAdapter::kIntervalCount - 1), .defaultValue = 3.0f, .kind = ParamKind::IntRound, .offset = offsetof(PingPongDelayParams, interval), .flags = kParamClamp },
            { .id = 1, .name = "delay", .minValue = 1.0f, .maxValue = 2000.0f, .defaultValue = 375.0f, .offset = offsetof(PingPongDelayParams, timeMs), .flags = kParamClamp },
            { .id = 2, .name = "feedback", .minValue = 0.0f, .maxValue = 0.95f, .defaultValue = 0.45f, .offset = offsetof(PingPongDelayParams, feedback), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 3, .name = "mix", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.35f, .offset = offsetof(PingPongDelayParams, mix), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 4, .name = "tone", .minValue = 200.0f, .maxValue = 18000.0f, .defaultValue = 6000.0f, .offset = offsetof(PingPongDelayParams, tone), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 5, .name = "width", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(PingPongDelayParams, width), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         // The plan folds a mono cable into both sides, so the input is always two channels here.
         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In", PortChannels::Stereo } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         constexpr double kIntervalBeats[PingPongDelayModuleAdapter::kIntervalCount] = {
            0.0, // ms
            1.0, // 1/4
            0.75, // 1/8 dotted
            0.5, // 1/8
            1.0 / 3.0, // 1/8 triplet
            0.25, // 1/16
            2.0, // 1/2
            1.5, // 1/4 dotted
         };

         const std::vector<SliderSpec>& pingPongSpecs()
         {
            static const std::vector<SliderSpec> specs =
            slidersFromParams(kParams,
                              {
                              { "interval", "Interval", SliderFormat::Choice, { "ms", "1/4", "1/8.", "1/8", "1/8T", "1/16", "1/2", "1/4." } },
                              { "delay", "Time", SliderFormat::Millis },
                              { "feedback", "Feedback", SliderFormat::Percent },
                              { "mix", "Mix", SliderFormat::Percent },
                              { "tone", "Tone", SliderFormat::Hertz, {}, true },
                              { "width", "Width", SliderFormat::Percent },
                              });
            return specs;
         }
      }

      double PingPongDelayModuleAdapter::intervalBeats(int interval)
      {
         return kIntervalBeats[std::max(0, std::min(kIntervalCount - 1, interval))];
      }

      int PingPongDelayModuleAdapter::delaySamples(const PingPongDelayParams& params, float bpm, float sampleRate)
      {
         const double beats = intervalBeats(params.interval);
         const double seconds = beats > 0.0 ? beats * 60.0 / std::max(1.0, static_cast<double>(bpm))
                                            : static_cast<double>(params.timeMs) * 0.001;
         const double samples = std::round(seconds * sampleRate);
         return static_cast<int>(std::max(1.0, std::min(samples, static_cast<double>(PingPongDelayRuntimeState::kMaxSamples - 1))));
      }

      int PingPongDelayModuleAdapter::outputChannelCount(const void* params, const WasmAudioProcessContext& context) const
      {
         (void)params;
         (void)context;
         return 2;
      }

      ParamTable PingPongDelayModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> PingPongDelayModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> PingPongDelayModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> PingPongDelayModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), pingPongSpecs(), 170.0f);
      }

      void PingPongDelayModuleAdapter::processAudio(void* runtimeState,
                                                    const void* paramsPtr,
                                                    float* const* channels,
                                                    const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels || context.numChannels < 2)
            return;
         auto& state = stateOf(runtimeState);
         const auto& params = paramsOf(paramsPtr);
         const float sampleRate = std::max(1.0f, context.sampleRate);
         const size_t capacity = PingPongDelayRuntimeState::kMaxSamples;
         const size_t delay = static_cast<size_t>(delaySamples(params, context.bpm, sampleRate));
         const float toneCoeff = 1.0f - std::exp(-6.28318530718f * std::min(params.tone, sampleRate * 0.45f) / sampleRate);
         const float tapNear = 0.5f + 0.5f * params.width;
         const float tapFar = 1.0f - tapNear;
         const float dry = 1.0f - params.mix;

         float* left = channels[0];
         float* right = channels[1];
         float* lineL = state.lines[0];
         float* lineR = state.lines[1];
         for (int i = 0; i < context.numSamples; ++i)
         {
            const size_t read = (state.writeIndex + capacity - delay) % capacity;
            const float tapL = lineL[read];
            const float tapR = lineR[read];
            state.toneState[0] += toneCoeff * (tapL - state.toneState[0]);
            state.toneState[1] += toneCoeff * (tapR - state.toneState[1]);
            const float input = 0.5f * (left[i] + right[i]);
            lineL[state.writeIndex] = input + params.feedback * state.toneState[1];
            lineR[state.writeIndex] = params.feedback * state.toneState[0];
            state.writeIndex = (state.writeIndex + 1) % capacity;

            left[i] = left[i] * dry + params.mix * (tapL * tapNear + tapR * tapFar);
            right[i] = right[i] * dry + params.mix * (tapR * tapNear + tapL * tapFar);
         }
      }

      BESPOKE_REGISTER_MODULE(PingPongDelayModuleAdapter);

   } // namespace wasm
} // namespace bespoke
