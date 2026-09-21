/**
 * BespokeSynth WASM - Two-voice modulated-delay chorus adapter implementation
 */

#include "BespokeWasm/adapters/ChorusModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
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
            };
            return specs;
         }

         float readRing(const ChorusRuntimeState& state, float delaySamples)
         {
            constexpr int kMask = ChorusRuntimeState::kRingSize - 1;
            const float readPos = static_cast<float>(state.writeIndex) - delaySamples;
            const float floorPos = std::floor(readPos);
            const float frac = readPos - floorPos;
            const int i0 = static_cast<int>(floorPos) & kMask;
            const int i1 = (i0 + 1) & kMask;
            return state.ring[i0] + (state.ring[i1] - state.ring[i0]) * frac;
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
                                             float* buffer,
                                             const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<ChorusRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const ChorusParams*>(paramsPtr);
         const float sampleRate = std::max(1.0f, context.sampleRate);
         const float msToSamples = sampleRate * 0.001f;
         const float maxDelay = static_cast<float>(ChorusRuntimeState::kRingSize - 2);
         const float phaseStep = params.rateHz / sampleRate;
         const float excursionMs = params.depth * kMaxExcursionMs;
         const float dryGain = 1.0f - params.mix * 0.5f;
         constexpr int kMask = ChorusRuntimeState::kRingSize - 1;

         for (int i = 0; i < context.numSamples; ++i)
         {
            const float input = buffer[i];
            // Mod CV (-1..1) pushes the delay line further, up to one more excursion.
            const float modMs = context.modulationBuffer ? context.modulationBuffer[i] * kMaxExcursionMs : 0.0f;

            state.ring[state.writeIndex] = input + state.lastWet * params.feedback;

            float wet = 0.0f;
            for (int v = 0; v < ChorusRuntimeState::kVoices; ++v)
            {
               const float phase = state.lfoPhase + static_cast<float>(v) / ChorusRuntimeState::kVoices;
               const float lfo = std::sin(kTwoPi * phase);
               const float delayMs = params.delayMs + excursionMs * lfo + modMs;
               const float delay = std::max(1.0f, std::min(maxDelay, delayMs * msToSamples));
               wet += readRing(state, delay);
            }
            wet *= 1.0f / ChorusRuntimeState::kVoices;
            state.lastWet = wet;

            state.writeIndex = (state.writeIndex + 1) & kMask;
            state.lfoPhase += phaseStep;
            if (state.lfoPhase >= 1.0f)
               state.lfoPhase -= 1.0f;

            buffer[i] = input * dryGain + wet * params.mix;
         }
      }

      BESPOKE_REGISTER_MODULE(ChorusModuleAdapter);

   } // namespace wasm
} // namespace bespoke
