/**
 * BespokeSynth WASM - Sampler adapter
 */

#include "BespokeWasm/adapters/SamplerModuleAdapter.h"
#include "BespokeWasm/BundledKit.h"
#include "BespokeWasm/SampleSource.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/TuningTable.h"
#include "BespokeWasm/modules/WasmModules.h"
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
            { .id = 0, .name = "volume", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.85f, .offset = offsetof(SamplerParams, volume), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "start", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .offset = offsetof(SamplerParams, start), .flags = kParamClamp },
            { .id = 2, .name = "end", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(SamplerParams, end), .flags = kParamClamp },
            { .id = 3, .name = "loopStart", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .offset = offsetof(SamplerParams, loopStart), .flags = kParamClamp },
            { .id = 4, .name = "loopEnd", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(SamplerParams, loopEnd), .flags = kParamClamp },
            { .id = 5, .name = "mode", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f, .kind = ParamKind::Int, .offset = offsetof(SamplerParams, mode) },
            { .id = 6, .name = "root", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f, .kind = ParamKind::IntRound, .offset = offsetof(SamplerParams, rootPitch) },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         float midiRate(const float* noteFrequencies, int pitch, int root)
         {
            const float rootHz = noteFrequency(noteFrequencies, root);
            return rootHz > 0.0f ? noteFrequency(noteFrequencies, pitch) / rootHz : 1.0f;
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

      ParamTable SamplerModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> SamplerModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> SamplerModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> SamplerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SamplerModule>(id);
      }

      void SamplerModuleAdapter::bindParams(int moduleId, const WasmStringMap& extras, void* paramsPtr) const
      {
         (void)moduleId;
         auto& params = paramsOf(paramsPtr);
         bindSampleFromExtras(extras, params.sample, params.handle);
      }

      void SamplerModuleAdapter::finalizeParams(void* paramsPtr) const
      {
         auto& params = paramsOf(paramsPtr);
         if (params.end <= params.start)
            params.end = std::min(1.0f, params.start + 0.01f);
         if (params.loopEnd <= params.loopStart)
            params.loopEnd = std::min(1.0f, params.loopStart + 0.01f);
      }

      void SamplerModuleAdapter::rebindParams(int moduleId, void* paramsPtr) const
      {
         (void)moduleId;
         auto* params = static_cast<SamplerParams*>(paramsPtr);
         // The UI module copied the buffer to this heap under the same id and generation before the snapshot.
         rebindSample(params->sample, params->handle);
      }

      void SamplerModuleAdapter::collectResources(int moduleId, const void* paramsPtr, WasmResourceRefs& refs) const
      {
         (void)moduleId;
         if (const auto* params = static_cast<const SamplerParams*>(paramsPtr))
            collectSample(params->sample, params->handle, refs);
      }

      int SamplerModuleAdapter::outputChannelCount(const void* paramsPtr, const WasmAudioProcessContext& context) const
      {
         (void)context;
         const auto* params = static_cast<const SamplerParams*>(paramsPtr);
         return params && params->sample ? params->sample->channelCount() : 1;
      }

      void SamplerModuleAdapter::processAudio(void* runtimeState,
                                              const void* paramsPtr,
                                              float* const* channels,
                                              const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;

         auto& state = *static_cast<SamplerAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const SamplerParams*>(paramsPtr);
         const SampleBuffer* sample = params.sample;
         const int frames = sample ? sample->frameCount() : 0;
         const float* data = sample ? sample->channelData(0) : nullptr;
         // A stereo file into a one-channel block (processMono) plays its mid.
         const bool stereoSample = sample && sample->channelCount() == 2;
         const float* dataR = stereoSample ? sample->channelData(1) : data;
         const int outChannels = context.numChannels;

         if (context.notes && context.noteCount > 0 && data && frames > 0)
         {
            for (int i = 0; i < context.noteCount; ++i)
            {
               const auto& note = context.notes[i];
               if (isPitchBendEvent(note))
               {
                  if (SamplerVoice* voice = findVoice(state, note.pitch))
                     voice->rate = midiRate(context.noteFrequencies, note.pitch, params.rootPitch) * pitchBendRatio(note.pitchBend);
               }
               else if (note.isNoteOn)
               {
                  SamplerVoice* voice = stealVoice(state);
                  voice->active = true;
                  voice->gated = true;
                  voice->pitch = note.pitch;
                  voice->velocity = std::max(0.0f, std::min(1.0f, note.velocity));
                  voice->position = static_cast<double>(params.start) * frames;
                  voice->rate = midiRate(context.noteFrequencies, note.pitch, params.rootPitch) * pitchBendRatio(note.pitchBend);
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

         for (int ch = 0; ch < outChannels; ++ch)
            std::fill(channels[ch], channels[ch] + context.numSamples, 0.0f);
         if (!data || frames <= 0)
            return;

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
               const float gain = voice.velocity * volume;
               const float left = data[idx] + (data[next] - data[idx]) * frac;
               const float right = dataR[idx] + (dataR[next] - dataR[idx]) * frac;
               if (outChannels == 2)
               {
                  channels[0][i] += left * gain;
                  channels[1][i] += right * gain;
               }
               else
               {
                  channels[0][i] += (stereoSample ? (left + right) * 0.5f : left) * gain;
               }
               voice.position += voice.rate;
            }
         }
      }

      BESPOKE_REGISTER_MODULE(SamplerModuleAdapter);

   } // namespace wasm
} // namespace bespoke
