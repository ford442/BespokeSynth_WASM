/**
 * BespokeSynth WASM - Sampler adapter
 */

#include "BespokeWasm/adapters/SamplerModuleAdapter.h"
#include "BespokeWasm/SampleStore.h"
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
         float clamp01(float value)
         {
            return std::max(0.0f, std::min(1.0f, value));
         }

         float midiRate(int pitch, int root)
         {
            return std::pow(2.0f, static_cast<float>(pitch - root) / 12.0f);
         }

         SamplerVoice* stealVoice(SamplerAdapterRuntimeState& state)
         {
            SamplerVoice* oldest = &state.voices[0];
            for (int i = 0; i < kSamplerVoiceCount; ++i)
            {
               if (!state.voices[i].active)
                  return &state.voices[i];
               if (state.voices[i].age < oldest->age)
                  oldest = &state.voices[i];
            }
            return oldest;
         }

         SamplerVoice* findVoice(SamplerAdapterRuntimeState& state, int pitch)
         {
            for (int i = 0; i < kSamplerVoiceCount; ++i)
            {
               if (state.voices[i].active && state.voices[i].pitch == pitch)
                  return &state.voices[i];
            }
            return nullptr;
         }
      }

      std::vector<WasmControlDescriptor> SamplerModuleAdapter::controlDescriptors() const
      {
         return {
            { "volume", 0.85f },
            { "start", 0.0f },
            { "end", 1.0f },
            { "loopStart", 0.0f },
            { "loopEnd", 1.0f },
            { "mode", 0.0f },
            { "root", 60.0f },
         };
      }

      std::vector<PortDescriptor> SamplerModuleAdapter::inputPorts() const
      {
         return { { PortType::Note, "Notes" } };
      }

      std::vector<PortDescriptor> SamplerModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> SamplerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SamplerModule>(id);
      }

      void SamplerModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         fillParams(-1, controls, {}, dst);
      }

      void SamplerModuleAdapter::fillParams(int moduleId,
                                            const WasmControlMap& controls,
                                            const WasmStringMap& extras,
                                            void* dst) const
      {
         (void)moduleId;
         auto* params = new (dst) SamplerParams();
         params->volume = clamp01(wasmControlValue(controls, "volume", 0.85f));
         params->start = clamp01(wasmControlValue(controls, "start", 0.0f));
         params->end = clamp01(wasmControlValue(controls, "end", 1.0f));
         params->loopStart = clamp01(wasmControlValue(controls, "loopStart", 0.0f));
         params->loopEnd = clamp01(wasmControlValue(controls, "loopEnd", 1.0f));
         params->mode = static_cast<int>(wasmControlValue(controls, "mode", 0.0f));
         params->rootPitch = static_cast<int>(std::lround(wasmControlValue(controls, "root", 60.0f)));
         if (params->end <= params->start)
            params->end = std::min(1.0f, params->start + 0.01f);
         if (params->loopEnd <= params->loopStart)
            params->loopEnd = std::min(1.0f, params->loopStart + 0.01f);

         auto extra = extras.find("sampleHash");
         if (extra != extras.end() && !extra->second.empty())
            params->sample = SampleStore::instance().findByHash(extra->second);
      }

      void SamplerModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) SamplerAdapterRuntimeState();
      }

      void SamplerModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<SamplerAdapterRuntimeState*>(runtimeState)->~SamplerAdapterRuntimeState();
      }

      void SamplerModuleAdapter::processAudio(void* runtimeState,
                                              const void* paramsPtr,
                                              float* buffer,
                                              const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<SamplerAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const SamplerParams*>(paramsPtr);
         const SampleBuffer* sample = params.sample;
         const int frames = sample ? sample->frameCount() : 0;
         const float* data = sample ? sample->samples() : nullptr;

         if (context.notes && context.noteCount > 0 && data && frames > 0)
         {
            for (int i = 0; i < context.noteCount; ++i)
            {
               const auto& note = context.notes[i];
               if (note.isNoteOn)
               {
                  SamplerVoice* voice = stealVoice(state);
                  voice->active = true;
                  voice->gated = true;
                  voice->pitch = note.pitch;
                  voice->velocity = std::max(0.0f, std::min(1.0f, note.velocity));
                  voice->position = static_cast<double>(params.start) * frames;
                  voice->rate = midiRate(note.pitch, params.rootPitch);
                  voice->age = ++state.ageCounter;
               }
               else if (SamplerVoice* voice = findVoice(state, note.pitch))
               {
                  voice->gated = false;
                  if (params.mode == static_cast<int>(SamplerPlayMode::Gate))
                     voice->active = false;
               }
            }
         }

         for (int i = 0; i < context.numSamples; ++i)
            buffer[i] = 0.0f;
         if (!data || frames <= 0)
            return;

         const double start = static_cast<double>(params.start) * frames;
         const double end = static_cast<double>(params.end) * frames;
         const double loopStart = static_cast<double>(params.loopStart) * frames;
         const double loopEnd = static_cast<double>(params.loopEnd) * frames;
         const float volume = params.volume;

         for (int v = 0; v < kSamplerVoiceCount; ++v)
         {
            SamplerVoice& voice = state.voices[v];
            if (!voice.active)
               continue;

            for (int i = 0; i < context.numSamples; ++i)
            {
               if (voice.position >= end || voice.position >= frames)
               {
                  if (params.mode == static_cast<int>(SamplerPlayMode::Loop) ||
                      (params.mode == static_cast<int>(SamplerPlayMode::Gate) && voice.gated))
                  {
                     voice.position = loopStart;
                  }
                  else
                  {
                     voice.active = false;
                     break;
                  }
               }

               if (params.mode == static_cast<int>(SamplerPlayMode::Loop) && voice.position >= loopEnd)
                  voice.position = loopStart;

               const int idx = static_cast<int>(voice.position);
               const int next = std::min(frames - 1, idx + 1);
               const float frac = static_cast<float>(voice.position - idx);
               const float sampleValue = data[idx] + (data[next] - data[idx]) * frac;
               buffer[i] += sampleValue * voice.velocity * volume;
               voice.position += voice.rate;
            }
         }
      }

      BESPOKE_REGISTER_MODULE(SamplerModuleAdapter);

   } // namespace wasm
} // namespace bespoke
