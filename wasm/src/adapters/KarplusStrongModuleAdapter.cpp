/**
 * BespokeSynth WASM - Polyphonic Karplus-Strong adapter implementation
 */

#include "BespokeWasm/adapters/KarplusStrongModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
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
         constexpr int kSourceSin = 0;
         constexpr int kSourceNoise = 1;
         constexpr int kSourceMix = 2;
         constexpr int kSourceSaw = 3;

         // The resonance builds well above the exciter's level; this keeps a full chord near 0 dBFS.
         constexpr float kVoiceGain = 0.3f;
         constexpr float kSilent = 1.0e-4f;
         constexpr int kQuietChunksToEnd = 8;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "vol", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 1.0f, .offset = offsetof(KarplusStrongParams, vol), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "invert", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Bool, .offset = offsetof(KarplusStrongParams, invert) },
            { .id = 2, .name = "filter", .minValue = 0.0f, .maxValue = 5.0f, .defaultValue = 1.0f, .offset = offsetof(KarplusStrongParams, filter), .flags = kParamClamp },
            { .id = 3, .name = "feedback", .minValue = 0.5f, .maxValue = 0.9999f, .defaultValue = 0.98f, .offset = offsetof(KarplusStrongParams, feedback), .flags = kParamClamp },
            { .id = 4, .name = "source", .minValue = 0.0f, .maxValue = 3.0f, .defaultValue = 2.0f, .kind = ParamKind::IntRound, .offset = offsetof(KarplusStrongParams, source), .flags = kParamClamp },
            { .id = 5, .name = "xfreq", .minValue = 10.0f, .maxValue = 1000.0f, .defaultValue = 100.0f, .offset = offsetof(KarplusStrongParams, exciterFreq), .flags = kParamClamp },
            { .id = 6, .name = "xatt", .minValue = 0.01f, .maxValue = 40.0f, .defaultValue = 1.0f, .offset = offsetof(KarplusStrongParams, exciterAttack), .flags = kParamClamp },
            { .id = 7, .name = "xdec", .minValue = 0.01f, .maxValue = 40.0f, .defaultValue = 3.0f, .offset = offsetof(KarplusStrongParams, exciterDecay), .flags = kParamClamp },
            { .id = 8, .name = "vel2vol", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(KarplusStrongParams, velToVolume), .flags = kParamClamp },
            { .id = 9, .name = "pitchtone", .minValue = -2.0f, .maxValue = 2.0f, .defaultValue = 0.0f, .offset = offsetof(KarplusStrongParams, pitchTone), .flags = kParamClamp },
            { .id = 10, .name = "release", .minValue = 10.0f, .maxValue = 4000.0f, .defaultValue = 400.0f, .offset = offsetof(KarplusStrongParams, release), .flags = kParamClamp },
            { .id = 11, .name = "voices", .minValue = 1.0f, .maxValue = static_cast<float>(kPolyMaxVoices), .defaultValue = static_cast<float>(kPolyMaxVoices), .kind = ParamKind::IntRound, .offset = offsetof(KarplusStrongParams, voices), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& karplusSpecs()
         {
            static const std::vector<SliderSpec> specs =
            slidersFromParams(kParams,
                              {
                              { "vol", "Volume", SliderFormat::Percent },
                              { "invert", "Invert", SliderFormat::Choice, { "off", "on" } },
                              { "filter", "Filter", SliderFormat::Decimal },
                              { "feedback", "Feedback", SliderFormat::Decimal },
                              { "source", "Source", SliderFormat::Choice, { "sin", "noise", "mix", "saw" } },
                              { "xfreq", "Exciter freq", SliderFormat::Hertz, {}, true },
                              { "xatt", "Exciter att", SliderFormat::Millis },
                              { "xdec", "Exciter dec", SliderFormat::Millis },
                              { "vel2vol", "Vel > vol", SliderFormat::Percent },
                              { "pitchtone", "Pitch tone", SliderFormat::Decimal },
                              { "release", "Release", SliderFormat::Millis, {}, true },
                              { "voices", "Voices", SliderFormat::Integer },
                              });
            return specs;
         }

         float noiseSample(uint32_t& state)
         {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
         }

         float pitchOf(float frequency)
         {
            return 69.0f + 12.0f * std::log2(std::max(1.0f, frequency) / 440.0f);
         }
      }

      ParamTable KarplusStrongModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> KarplusStrongModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> KarplusStrongModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> KarplusStrongModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), karplusSpecs(), 180.0f);
      }

      dsp::EnvelopeSettings KarplusStrongModuleAdapter::ampEnvelope(const KarplusStrongParams& params)
      {
         // The string makes its own envelope; this only fades the tail on note-off (the desktop mute ramp).
         return { 0.5f, 1.0f, 1.0f, params.release };
      }

      float KarplusStrongModuleAdapter::ampPeak(const KarplusStrongParams& params, float velocity)
      {
         return (1.0f - params.velToVolume) + params.velToVolume * velocity * velocity;
      }

      void KarplusStrongModuleAdapter::startVoice(KarplusStrongVoice& voice,
                                                  const PolyVoiceSlot& slot,
                                                  const KarplusStrongParams& params,
                                                  float sampleRate) const
      {
         (void)params;
         (void)sampleRate;
         // A stolen voice keeps its ringing string, as a real re-pluck would; the exciter restarts.
         voice.exciterFrames = 0;
         voice.oscPhase = 0.25f; // Start(): pi / 2 keeps the exciter DC-centred
         voice.quietChunks = 0;
         voice.noise = 0x9e3779b9u * static_cast<uint32_t>(slot.pitch + 1) + slot.age;
      }

      bool KarplusStrongModuleAdapter::renderVoice(KarplusStrongVoice& voice,
                                                   const PolyVoiceSlot& slot,
                                                   const KarplusStrongParams& params,
                                                   float* const* out,
                                                   int count,
                                                   float sampleRate) const
      {
         constexpr int kMask = KarplusStrongVoice::kDelaySize - 1;
         // Inverting the loop halves the resonant frequency; the desktop corrects with an octave up.
         const float frequency = slot.frequency * (params.invert ? 2.0f : 1.0f);
         const float pitch = pitchOf(frequency);
         const float filterRate = params.filter * std::pow(frequency / 300.0f, std::exp2(params.pitchTone));
         const float filterLerp = std::max(0.0f, std::min(1.0f, std::exp2(-filterRate)));
         // The loop lowpass delays the low harmonics by a / (1 - a) samples; take that off the line so the
         // string rings at its pitch (Source/KarplusStrongVoice.cpp does not, and plays flat with heavy filtering).
         const float filterDelay = filterLerp / std::max(1.0e-3f, 1.0f - filterLerp);
         const float samplesAgo = std::max(2.0f, std::min(static_cast<float>(KarplusStrongVoice::kDelaySize - 2),
                                                          sampleRate / std::max(1.0f, frequency) - filterDelay));
         const int delayWhole = static_cast<int>(samplesAgo);
         const float delayFrac = samplesAgo - static_cast<float>(delayWhole);
         const float feedbackGain = std::sqrt(params.feedback) * (params.invert ? -1.0f : 1.0f);
         const float phaseStep = params.exciterFreq / sampleRate;
         const int attackFrames = std::max(1, static_cast<int>(params.exciterAttack * 0.001f * sampleRate));
         const int decayFrames = std::max(1, static_cast<int>(params.exciterDecay * 0.001f * sampleRate));
         float pitchBlend = std::max(0.0f, std::min(1.0f, (pitch - 40.0f) / 60.0f));
         pitchBlend *= pitchBlend;
         const float gain = params.vol * kVoiceGain;

         float* dst = out[0];
         float peak = 0.0f;
         for (int i = 0; i < count; ++i)
         {
            float exciter = 0.0f;
            const int frame = voice.exciterFrames;
            if (frame < attackFrames + decayFrames)
            {
               const float env = frame < attackFrames ? static_cast<float>(frame) / static_cast<float>(attackFrames)
                                                      : 1.0f - static_cast<float>(frame - attackFrames) / static_cast<float>(decayFrames);
               voice.oscPhase += phaseStep;
               voice.oscPhase -= std::floor(voice.oscPhase);
               const float sine = dsp::SineTable::at(voice.oscPhase);
               const float saw = 2.0f * voice.oscPhase - 1.0f;
               switch (params.source)
               {
                  case kSourceSin:
                     exciter = sine;
                     break;
                  case kSourceNoise:
                     exciter = noiseSample(voice.noise);
                     break;
                  case kSourceSaw:
                     exciter = saw;
                     break;
                  case kSourceMix:
                  default:
                     exciter = noiseSample(voice.noise) * pitchBlend + sine * (1.0f - pitchBlend);
                     break;
               }
               exciter *= env;
               ++voice.exciterFrames;
            }

            const float a = voice.delay[(voice.writeIndex - delayWhole) & kMask];
            const float b = voice.delay[(voice.writeIndex - delayWhole - 1) & kMask];
            const float delayed = a + (b - a) * delayFrac;
            voice.filtered = delayed + (voice.filtered - delayed) * filterLerp;
            const float sample = exciter + voice.filtered * feedbackGain;
            voice.delay[voice.writeIndex] = sample;
            voice.writeIndex = (voice.writeIndex + 1) & kMask;
            dst[i] = sample * gain;
            peak = std::max(peak, std::fabs(sample));
         }

         if (voice.exciterFrames >= attackFrames + decayFrames && peak < kSilent)
         {
            if (++voice.quietChunks >= kQuietChunksToEnd)
            {
               std::fill(voice.delay, voice.delay + KarplusStrongVoice::kDelaySize, 0.0f);
               voice.filtered = 0.0f;
               return false;
            }
         }
         else
         {
            voice.quietChunks = 0;
         }
         return true;
      }

      BESPOKE_REGISTER_MODULE(KarplusStrongModuleAdapter);

   } // namespace wasm
} // namespace bespoke
