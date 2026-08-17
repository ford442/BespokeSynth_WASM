/**
 * BespokeSynth WASM - Oscillator source adapter implementation
 */

#include "BespokeWasm/adapters/OscillatorModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
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

         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }

         void updateOscillatorType(OscillatorAdapterRuntimeState& state, int waveform)
         {
            switch (waveform % 4)
            {
               case 1:
                  state.oscillatorType = kOsc_Saw;
                  break;
               case 2:
                  state.oscillatorType = kOsc_Square;
                  break;
               case 3:
                  state.oscillatorType = kOsc_Tri;
                  break;
               default:
                  state.oscillatorType = kOsc_Sin;
                  break;
            }
            state.oscillator.SetType(state.oscillatorType);
         }

         float renderOscillatorSample(OscillatorAdapterRuntimeState& state,
                                      float frequency,
                                      float sampleRate)
         {
            const float sample = state.oscillator.Value(state.phase);
            state.phase += kTwoPi * clampFloat(frequency, 0.0f, sampleRate * 0.45f) / sampleRate;
            if (state.phase >= kTwoPi)
               state.phase = std::fmod(state.phase, kTwoPi);
            return sample;
         }
      }

      std::vector<WasmControlDescriptor> OscillatorModuleAdapter::controlDescriptors() const
      {
         return {
            { "frequency", 440.0f },
            { "volume", 0.7f },
            { "waveform", 0.0f },
         };
      }

      std::vector<PortDescriptor> OscillatorModuleAdapter::inputPorts() const
      {
         return { { PortType::Note, "Pitch" }, { PortType::Modulation, "Mod" } };
      }

      std::vector<PortDescriptor> OscillatorModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> OscillatorModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<OscillatorModule>(id);
      }

      void OscillatorModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) OscillatorParams();
         params->frequency = wasmControlValue(controls, "frequency", 440.0f);
         params->volume = wasmControlValue(controls, "volume", 0.7f);
         params->waveform = static_cast<int>(wasmControlValue(controls, "waveform", 0.0f));
      }

      void OscillatorModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) OscillatorAdapterRuntimeState();
      }

      void OscillatorModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<OscillatorAdapterRuntimeState*>(runtimeState)->~OscillatorAdapterRuntimeState();
      }

      void OscillatorModuleAdapter::processAudio(void* runtimeState,
                                                 const void* paramsPtr,
                                                 float* buffer,
                                                 const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<OscillatorAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const OscillatorParams*>(paramsPtr);
         updateOscillatorType(state, params.waveform);

         if (context.notes && context.noteCount > 0)
         {
            for (int noteIndex = 0; noteIndex < context.noteCount; ++noteIndex)
            {
               const auto& note = context.notes[noteIndex];
               if (note.isNoteOn)
               {
                  state.noteFrequency = 440.0f * std::pow(2.0f, (note.pitch - 69) / 12.0f);
                  state.noteVelocity = clampFloat(note.velocity, 0.0f, 1.0f);
                  state.noteGate = true;
                  state.hasReceivedNote = true;
               }
               else
               {
                  state.noteGate = false;
                  state.hasReceivedNote = true;
               }
            }
         }

         const float frequency = state.hasReceivedNote ? state.noteFrequency : params.frequency;
         float level = 1.0f;
         if (context.hasNoteCable)
         {
            if (!state.hasReceivedNote || !state.noteGate)
               level = 0.0f;
            else
               level = state.noteVelocity;
         }
         else if (state.hasReceivedNote)
         {
            level = state.noteGate ? state.noteVelocity : 0.0f;
         }

         const float volume = clampFloat(params.volume, 0.0f, 1.0f);
         for (int i = 0; i < context.numSamples; ++i)
            buffer[i] = renderOscillatorSample(state, frequency, context.sampleRate) * volume * level;
      }

      BESPOKE_REGISTER_MODULE(OscillatorModuleAdapter);

   } // namespace wasm
} // namespace bespoke
