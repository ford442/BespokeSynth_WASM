/**
 * BespokeSynth WASM - Two-voice modulated-delay chorus adapter implementation
 */

#include "BespokeWasm/adapters/ChorusModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr float kTwoPi = 6.28318530717958647692f;
         constexpr float kMaxExcursionMs = 6.0f;

         const std::vector<SliderSpec>& chorusSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "rate", "Rate", 0.05f, 5.0f, 0.8f, SliderFormat::Hertz },
               { "depth", "Depth", 0.0f, 1.0f, 0.5f, SliderFormat::Percent },
               { "delay", "Delay", 5.0f, 30.0f, 15.0f, SliderFormat::Millis },
               { "feedback", "Fdbk", 0.0f, 0.9f, 0.0f, SliderFormat::Percent },
               { "mix", "Mix", 0.0f, 1.0f, 0.5f, SliderFormat::Percent },
               { "width", "Width", 0.0f, 1.0f, 1.0f, SliderFormat::Percent },
            };
            return specs;
         }

         float readRing(const float* ring, int writeIndex, float delaySamples)
         {
            constexpr int kMask = ChorusRuntimeState::kRingSize - 1;
            const float readPos = static_cast<float>(writeIndex) - delaySamples;
            const float floorPos = std::floor(readPos);
            const float frac = readPos - floorPos;
            const int i0 = static_cast<int>(floorPos) & kMask;
            const int i1 = (i0 + 1) & kMask;
            return ring[i0] + (ring[i1] - ring[i0]) * frac;
         }
      }

      std::vector<WasmControlDescriptor> ChorusModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(chorusSpecs());
      }

      std::vector<PortDescriptor> ChorusModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" }, { PortType::Modulation, "Mod" } };
      }

      std::vector<PortDescriptor> ChorusModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> ChorusModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), chorusSpecs());
      }

      void ChorusModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         const auto& specs = chorusSpecs();
         auto* params = new (dst) ChorusParams();
         params->rateHz = sliderSpecValue(specs, controls, "rate");
         params->depth = sliderSpecValue(specs, controls, "depth");
         params->delayMs = sliderSpecValue(specs, controls, "delay");
         params->feedback = sliderSpecValue(specs, controls, "feedback");
         params->mix = sliderSpecValue(specs, controls, "mix");
         params->width = sliderSpecValue(specs, controls, "width");
      }

      int ChorusModuleAdapter::outputChannelCount(const void* paramsPtr, const WasmAudioProcessContext& context) const
      {
         const auto* params = static_cast<const ChorusParams*>(paramsPtr);
         return (context.inputChannels == 2 || (params && params->width > 0.0f)) ? 2 : 1;
      }

      void ChorusModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) ChorusRuntimeState();
      }

      void ChorusModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<ChorusRuntimeState*>(runtimeState)->~ChorusRuntimeState();
      }

      void ChorusModuleAdapter::processAudio(void* runtimeState,
                                             const void* paramsPtr,
                                             float* const* channels,
                                             const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;

         auto& state = *static_cast<ChorusRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const ChorusParams*>(paramsPtr);
         const float sampleRate = std::max(1.0f, context.sampleRate);
         const float msToSamples = sampleRate * 0.001f;
         const float maxDelay = static_cast<float>(ChorusRuntimeState::kRingSize - 2);
         const float phaseStep = params.rateHz / sampleRate;
         const float excursionMs = params.depth * kMaxExcursionMs;
         const float dryGain = 1.0f - params.mix * 0.5f;
         const bool stereo = context.numChannels == 2;
         const float width = std::max(0.0f, std::min(1.0f, params.width));
         constexpr int kMask = ChorusRuntimeState::kRingSize - 1;

         // Going mono → stereo: seed the right ring from the left so the tail does not jump.
         if (stereo && state.lastChannels == 1)
         {
            std::memcpy(state.ring[1], state.ring[0], sizeof(state.ring[0]));
            state.lastWet[1] = state.lastWet[0];
         }
         state.lastChannels = context.numChannels;

         for (int i = 0; i < context.numSamples; ++i)
         {
            // Mod CV (-1..1) pushes the delay line further, up to one more excursion.
            const float modMs = context.modulationBuffer ? context.modulationBuffer[i] * kMaxExcursionMs : 0.0f;
            const int ringCount = stereo ? 2 : 1;
            for (int ch = 0; ch < ringCount; ++ch)
               state.ring[ch][state.writeIndex] = channels[ch][i] + state.lastWet[ch] * params.feedback;

            // Voice v reads ring v in stereo (A from L, B from R); both read ring 0 in mono.
            float voice[ChorusRuntimeState::kVoices];
            for (int v = 0; v < ChorusRuntimeState::kVoices; ++v)
            {
               const float phase = state.lfoPhase + static_cast<float>(v) / ChorusRuntimeState::kVoices;
               const float lfo = std::sin(kTwoPi * phase);
               const float delayMs = params.delayMs + excursionMs * lfo + modMs;
               const float delay = std::max(1.0f, std::min(maxDelay, delayMs * msToSamples));
               voice[v] = readRing(state.ring[stereo ? v : 0], state.writeIndex, delay);
            }
            const float mid = (voice[0] + voice[1]) * 0.5f;
            const float side = (voice[0] - voice[1]) * 0.5f * width;

            state.writeIndex = (state.writeIndex + 1) & kMask;
            state.lfoPhase += phaseStep;
            if (state.lfoPhase >= 1.0f)
               state.lfoPhase -= 1.0f;

            if (stereo)
            {
               state.lastWet[0] = mid + side;
               state.lastWet[1] = mid - side;
               channels[0][i] = channels[0][i] * dryGain + state.lastWet[0] * params.mix;
               channels[1][i] = channels[1][i] * dryGain + state.lastWet[1] * params.mix;
            }
            else
            {
               state.lastWet[0] = mid;
               channels[0][i] = channels[0][i] * dryGain + mid * params.mix;
            }
         }
      }

      BESPOKE_REGISTER_MODULE(ChorusModuleAdapter);

   } // namespace wasm
} // namespace bespoke
