/**
 * BespokeSynth WASM - Oscillator source adapter implementation
 */

#include "BespokeWasm/adapters/OscillatorModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/TuningTable.h"
#include "BespokeWasm/VoiceAllocator.h"
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
         // Voices render into a stack scratch chunk, then mix with a SIMD accumulate.
         constexpr int kVoiceChunk = 64;

         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }

         OscillatorType waveformType(int waveform)
         {
            switch (waveform % 4)
            {
               case 1:
                  return kOsc_Saw;
               case 2:
                  return kOsc_Square;
               case 3:
                  return kOsc_Tri;
               default:
                  return kOsc_Sin;
            }
         }

         void updateOscillatorTypes(OscillatorAdapterRuntimeState& state, int waveform)
         {
            const OscillatorType type = waveformType(waveform);
            state.oscillatorType = type;
            state.oscillator.SetType(type);
         }

         float renderSample(OscillatorAdapterRuntimeState& state, float& phase, float frequency, float sampleRate)
         {
            const float sample = state.oscillator.Value(phase);
            phase += kTwoPi * clampFloat(frequency, 0.0f, sampleRate * 0.45f) / sampleRate;
            if (phase >= kTwoPi)
               phase = std::fmod(phase, kTwoPi);
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
                                                 float* const* channels,
                                                 const WasmAudioProcessContext& context) const
      {
         float* buffer = channels ? channels[0] : nullptr;
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<OscillatorAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const OscillatorParams*>(paramsPtr);
         updateOscillatorTypes(state, params.waveform);

         if (context.notes && context.noteCount > 0)
         {
            for (int noteIndex = 0; noteIndex < context.noteCount; ++noteIndex)
            {
               const auto& note = context.notes[noteIndex];
               if (note.isNoteOn)
               {
                  OscillatorVoice* voice =
                  allocateVoice(state.voices, kOscillatorVoiceCount, state.ageCounter, note.pitch);
                  voice->gated = true;
                  voice->pitch = note.pitch;
                  voice->velocity = clampFloat(note.velocity, 0.0f, 1.0f);
                  voice->frequency = noteFrequency(context.noteFrequencies, note.pitch);
                  state.hasReceivedNote = true;
               }
               else if (OscillatorVoice* voice =
                        releaseVoice(state.voices, kOscillatorVoiceCount, note.pitch))
               {
                  voice->gated = false;
                  state.hasReceivedNote = true;
               }
            }
         }

         const float volume = clampFloat(params.volume, 0.0f, 1.0f);
         const bool useVoices = context.hasNoteCable || state.hasReceivedNote;
         if (!useVoices)
         {
            for (int i = 0; i < context.numSamples; ++i)
               buffer[i] = renderSample(state, state.freeRunPhase, params.frequency, context.sampleRate);
            dsp::scale(buffer, context.numSamples, volume);
            return;
         }

         bool anyGated = false;
         for (int v = 0; v < kOscillatorVoiceCount; ++v)
         {
            if (state.voices[v].gated)
            {
               anyGated = true;
               break;
            }
         }

         if (!anyGated)
         {
            for (int i = 0; i < context.numSamples; ++i)
               buffer[i] = 0.0f;
            return;
         }

         for (int i = 0; i < context.numSamples; ++i)
            buffer[i] = 0.0f;

         for (int v = 0; v < kOscillatorVoiceCount; ++v)
         {
            OscillatorVoice& voice = state.voices[v];
            if (!voice.gated)
               continue;
            const float level = voice.velocity * volume;
            float scratch[kVoiceChunk];
            for (int start = 0; start < context.numSamples; start += kVoiceChunk)
            {
               const int count = std::min(kVoiceChunk, context.numSamples - start);
               for (int i = 0; i < count; ++i)
                  scratch[i] = renderSample(state, voice.phase, voice.frequency, context.sampleRate);
               dsp::accumulateScaled(buffer + start, scratch, count, level);
            }
         }
      }

      BESPOKE_REGISTER_MODULE(OscillatorModuleAdapter);

   } // namespace wasm
} // namespace bespoke
