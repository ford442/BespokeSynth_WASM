/**
 * BespokeSynth WASM - Polyphonic granular synth adapter implementation
 */

#include "BespokeWasm/adapters/SeaOfGrainModuleAdapter.h"
#include "BespokeWasm/BundledKit.h"
#include "BespokeWasm/SamplePanelModule.h"
#include "BespokeWasm/SampleSource.h"
#include "BespokeWasm/dsp/SineTable.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr int kModePosition = 0;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "volume", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(SeaOfGrainParams, volume), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "mode", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .kind = ParamKind::IntRound, .offset = offsetof(SeaOfGrainParams, mode), .flags = kParamClamp },
            { .id = 2, .name = "pos", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.3f, .offset = offsetof(SeaOfGrainParams, position), .flags = kParamClamp },
            { .id = 3, .name = "overlap", .minValue = 0.25f, .maxValue = 16.0f, .defaultValue = 6.0f, .offset = offsetof(SeaOfGrainParams, overlap), .flags = kParamClamp },
            { .id = 4, .name = "lenMs", .minValue = 1.0f, .maxValue = 1000.0f, .defaultValue = 80.0f, .offset = offsetof(SeaOfGrainParams, lengthMs), .flags = kParamClamp },
            { .id = 5, .name = "posRandom", .minValue = 0.0f, .maxValue = 200.0f, .defaultValue = 5.0f, .offset = offsetof(SeaOfGrainParams, positionRandomMs), .flags = kParamClamp },
            { .id = 6, .name = "speedRandom", .minValue = 0.0f, .maxValue = 0.3f, .defaultValue = 0.0f, .offset = offsetof(SeaOfGrainParams, speedRandom), .flags = kParamClamp },
            { .id = 7, .name = "spacingRandom", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(SeaOfGrainParams, spacingRandom), .flags = kParamClamp },
            { .id = 8, .name = "octaves", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Bool, .offset = offsetof(SeaOfGrainParams, octaves) },
            { .id = 9, .name = "width", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(SeaOfGrainParams, width), .flags = kParamClamp },
            { .id = 10, .name = "speed", .minValue = -3.0f, .maxValue = 3.0f, .defaultValue = 1.0f, .offset = offsetof(SeaOfGrainParams, speed), .flags = kParamClamp },
            { .id = 11, .name = "root", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f, .kind = ParamKind::IntRound, .offset = offsetof(SeaOfGrainParams, root), .flags = kParamClamp },
            { .id = 12, .name = "keyboardBasePitch", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 36.0f, .kind = ParamKind::IntRound, .offset = offsetof(SeaOfGrainParams, keyboardBase), .flags = kParamClamp },
            { .id = 13, .name = "keyboardNumPitches", .minValue = 1.0f, .maxValue = 127.0f, .defaultValue = 24.0f, .kind = ParamKind::IntRound, .offset = offsetof(SeaOfGrainParams, keyboardRange), .flags = kParamClamp },
            { .id = 14, .name = "attack", .minValue = 1.0f, .maxValue = 4000.0f, .defaultValue = 20.0f, .offset = offsetof(SeaOfGrainParams, attack), .flags = kParamClamp },
            { .id = 15, .name = "release", .minValue = 1.0f, .maxValue = 4000.0f, .defaultValue = 400.0f, .offset = offsetof(SeaOfGrainParams, release), .flags = kParamClamp },
            { .id = 16, .name = "voices", .minValue = 1.0f, .maxValue = static_cast<float>(kPolyMaxVoices), .defaultValue = static_cast<float>(kPolyMaxVoices), .kind = ParamKind::IntRound, .offset = offsetof(SeaOfGrainParams, voices), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& grainSpecs()
         {
            static const std::vector<SliderSpec> specs =
            slidersFromParams(kParams,
                              {
                              { "volume", "Volume", SliderFormat::Percent },
                              { "mode", "Keys set", SliderFormat::Choice, { "position", "pitch" } },
                              { "pos", "Position", SliderFormat::Percent },
                              { "overlap", "Overlap", SliderFormat::Decimal },
                              { "lenMs", "Grain", SliderFormat::Millis, {}, true },
                              { "posRandom", "Pos rand", SliderFormat::Millis },
                              { "speedRandom", "Speed rand", SliderFormat::Decimal },
                              { "spacingRandom", "Spacing rand", SliderFormat::Percent },
                              { "octaves", "Octaves", SliderFormat::Choice, { "off", "on" } },
                              { "width", "Width", SliderFormat::Percent },
                              { "speed", "Speed", SliderFormat::Decimal },
                              { "root", "Root", SliderFormat::Integer },
                              { "keyboardBasePitch", "Key base", SliderFormat::Integer },
                              { "keyboardNumPitches", "Key range", SliderFormat::Integer },
                              { "attack", "Attack", SliderFormat::Millis, {}, true },
                              { "release", "Release", SliderFormat::Millis, {}, true },
                              { "voices", "Voices", SliderFormat::Integer },
                              });
            return specs;
         }

         /** [0, 1) */
         float nextUnit(uint32_t& state)
         {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float>(state >> 8) / 16777216.0f;
         }

         float hannWindow(float phase)
         {
            // 0.5 - 0.5 cos(2 pi phase), with the cosine as a quarter-cycle-shifted sine.
            return 0.5f - 0.5f * dsp::SineTable::at(phase + 0.25f);
         }
      }

      float SeaOfGrainModuleAdapter::voicePosition(const SeaOfGrainParams& params, int pitch)
      {
         float position = params.position;
         if (params.mode == kModePosition)
            position += static_cast<float>(pitch - params.keyboardBase) / static_cast<float>(std::max(1, params.keyboardRange));
         return std::max(0.0f, std::min(1.0f, position));
      }

      ParamTable SeaOfGrainModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> SeaOfGrainModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> SeaOfGrainModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> SeaOfGrainModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SamplePanelModule>(id, typeId(), displayName(), category(), grainSpecs(), 190.0f, "pos",
                                                    bundledTonalSamples()[0].sha256);
      }

      void SeaOfGrainModuleAdapter::bindParams(int moduleId, const WasmStringMap& extras, void* paramsPtr) const
      {
         (void)moduleId;
         auto& params = paramsOf(paramsPtr);
         bindSampleFromExtras(extras, params.sample, params.handle);
      }

      void SeaOfGrainModuleAdapter::rebindParams(int moduleId, void* paramsPtr) const
      {
         (void)moduleId;
         auto* params = static_cast<SeaOfGrainParams*>(paramsPtr);
         rebindSample(params->sample, params->handle);
      }

      void SeaOfGrainModuleAdapter::collectResources(int moduleId, const void* paramsPtr, WasmResourceRefs& refs) const
      {
         (void)moduleId;
         if (const auto* params = static_cast<const SeaOfGrainParams*>(paramsPtr))
            collectSample(params->sample, params->handle, refs);
      }

      dsp::EnvelopeSettings SeaOfGrainModuleAdapter::ampEnvelope(const SeaOfGrainParams& params)
      {
         return { params.attack, 1.0f, 1.0f, params.release };
      }

      void SeaOfGrainModuleAdapter::startVoice(SeaOfGrainVoice& voice,
                                               const PolyVoiceSlot& slot,
                                               const SeaOfGrainParams& params,
                                               float sampleRate) const
      {
         (void)params;
         (void)sampleRate;
         // Grains already sounding keep going; a retrigger only restarts the spray.
         voice.spawnCountdown = 0;
         voice.rng = 0x9e3779b9u ^ (static_cast<uint32_t>(slot.pitch) * 0x85ebca6bu) ^ slot.age;
         if (voice.rng == 0)
            voice.rng = 1;
      }

      bool SeaOfGrainModuleAdapter::renderVoice(SeaOfGrainVoice& voice,
                                                const PolyVoiceSlot& slot,
                                                const SeaOfGrainParams& params,
                                                float* const* out,
                                                int count,
                                                float sampleRate) const
      {
         float* left = out[0];
         float* right = out[1];
         std::fill(left, left + count, 0.0f);
         std::fill(right, right + count, 0.0f);
         const SampleBuffer* sample = params.sample;
         const int frames = sample ? sample->frameCount() : 0;
         if (frames <= 1)
            return true;
         const float* dataL = sample->channelData(0);
         const float* dataR = sample->channelCount() == 2 ? sample->channelData(1) : dataL;

         const float framesPerMs = sampleRate * 0.001f;
         const int grainLength = std::max(1, static_cast<int>(params.lengthMs * framesPerMs));
         const float spacing = static_cast<float>(grainLength) / params.overlap;
         const double basePosition = static_cast<double>(voicePosition(params, slot.pitch)) * (frames - 1);
         float baseSpeed = params.speed;
         if (params.mode != kModePosition)
            baseSpeed *= slot.frequency / (440.0f * std::exp2((static_cast<float>(params.root) - 69.0f) / 12.0f));
         // Granulator::ProcessFrame: thin out dense clouds from overlap 4 up.
         float density = 1.0f;
         if (params.overlap > 4.0f)
            density = 1.0f - 0.5f * (params.overlap - 4.0f) / 12.0f;
         const float gain = params.volume * density;

         for (int i = 0; i < count; ++i)
         {
            if (--voice.spawnCountdown <= 0)
            {
               Grain& grain = voice.grains[voice.nextGrain];
               voice.nextGrain = (voice.nextGrain + 1) % SeaOfGrainVoice::kMaxGrains;
               float speed = baseSpeed * (1.0f + (nextUnit(voice.rng) * 2.0f - 1.0f) * params.speedRandom);
               grain.volume = 1.0f;
               if (params.octaves)
               {
                  // Granulator::SpawnGrain: some fifths up (quieter), some fourths down.
                  const int pick = static_cast<int>(nextUnit(voice.rng) * 5.0f);
                  if (pick == 2)
                  {
                     speed *= 1.5f;
                     grain.volume = 0.5f;
                  }
                  else if (pick >= 3)
                  {
                     speed *= 0.75f;
                  }
               }
               const float jitterMs = (nextUnit(voice.rng) * 2.0f - 1.0f) * params.positionRandomMs;
               grain.active = true;
               grain.position = basePosition + jitterMs * framesPerMs;
               grain.speed = speed;
               grain.age = 0;
               grain.length = grainLength;
               grain.pan = (nextUnit(voice.rng) * 2.0f - 1.0f) * params.width;
               const float jitter = 1.0f + (nextUnit(voice.rng) - 0.5f) * params.spacingRandom;
               voice.spawnCountdown = std::max(1, static_cast<int>(spacing * jitter));
            }

            float sumL = 0.0f;
            float sumR = 0.0f;
            for (Grain& grain : voice.grains)
            {
               if (!grain.active)
                  continue;
               const float window = hannWindow(static_cast<float>(grain.age) / static_cast<float>(grain.length));
               const float level = window * grain.volume;
               sumL += readLinearWrapped(dataL, frames, grain.position) * level * (1.0f - grain.pan);
               sumR += readLinearWrapped(dataR, frames, grain.position) * level * (1.0f + grain.pan);
               grain.position += grain.speed;
               if (++grain.age >= grain.length)
                  grain.active = false;
            }
            left[i] = sumL * gain * 0.5f;
            right[i] = sumR * gain * 0.5f;
         }
         return true;
      }

      BESPOKE_REGISTER_MODULE(SeaOfGrainModuleAdapter);

   } // namespace wasm
} // namespace bespoke
