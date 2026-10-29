/**
 * BespokeSynth WASM - Polyphonic FM synth adapter implementation
 */

#include "BespokeWasm/adapters/FmSynthModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include "BespokeWasm/dsp/SineTable.h"
#include <algorithm>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         // Source/FMSynth.cpp's harmratio dropdown, in order.
         constexpr float kHarmonicRatios[] = { 0.125f, 0.2f, 0.25f, 1.0f / 3.0f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f };
         constexpr int kRatioCount = static_cast<int>(sizeof(kHarmonicRatios) / sizeof(kHarmonicRatios[0]));
         constexpr float kMaxRatioIndex = static_cast<float>(kRatioCount - 1);

         // Per-voice level: eight full voices sum to about 0 dBFS.
         constexpr float kVoiceGain = 0.15f;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "vol", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 1.0f, .offset = offsetof(FmSynthParams, vol), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "harmratio", .minValue = 0.0f, .maxValue = kMaxRatioIndex, .defaultValue = 5.0f, .kind = ParamKind::IntRound, .offset = offsetof(FmSynthParams, harmRatio), .flags = kParamClamp },
            { .id = 2, .name = "tweak", .minValue = 0.5f, .maxValue = 2.0f, .defaultValue = 1.0f, .offset = offsetof(FmSynthParams, tweak), .flags = kParamClamp },
            { .id = 3, .name = "mod", .minValue = 0.0f, .maxValue = 20.0f, .defaultValue = 2.0f, .offset = offsetof(FmSynthParams, mod), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 4, .name = "harmratio2", .minValue = 0.0f, .maxValue = kMaxRatioIndex, .defaultValue = 5.0f, .kind = ParamKind::IntRound, .offset = offsetof(FmSynthParams, harmRatio2), .flags = kParamClamp },
            { .id = 5, .name = "tweak2", .minValue = 0.5f, .maxValue = 2.0f, .defaultValue = 1.0f, .offset = offsetof(FmSynthParams, tweak2), .flags = kParamClamp },
            { .id = 6, .name = "mod2", .minValue = 0.0f, .maxValue = 20.0f, .defaultValue = 0.0f, .offset = offsetof(FmSynthParams, mod2), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 7, .name = "attack", .minValue = 1.0f, .maxValue = 2000.0f, .defaultValue = 5.0f, .offset = offsetof(FmSynthParams, attack), .flags = kParamClamp },
            { .id = 8, .name = "decay", .minValue = 1.0f, .maxValue = 4000.0f, .defaultValue = 300.0f, .offset = offsetof(FmSynthParams, decay), .flags = kParamClamp },
            { .id = 9, .name = "sustain", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.6f, .offset = offsetof(FmSynthParams, sustain), .flags = kParamClamp },
            { .id = 10, .name = "release", .minValue = 1.0f, .maxValue = 4000.0f, .defaultValue = 300.0f, .offset = offsetof(FmSynthParams, release), .flags = kParamClamp },
            { .id = 11, .name = "modAttack", .minValue = 1.0f, .maxValue = 2000.0f, .defaultValue = 1.0f, .offset = offsetof(FmSynthParams, modAttack), .flags = kParamClamp },
            { .id = 12, .name = "modDecay", .minValue = 1.0f, .maxValue = 4000.0f, .defaultValue = 400.0f, .offset = offsetof(FmSynthParams, modDecay), .flags = kParamClamp },
            { .id = 13, .name = "modSustain", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.3f, .offset = offsetof(FmSynthParams, modSustain), .flags = kParamClamp },
            { .id = 14, .name = "voices", .minValue = 1.0f, .maxValue = static_cast<float>(kPolyMaxVoices), .defaultValue = static_cast<float>(kPolyMaxVoices), .kind = ParamKind::IntRound, .offset = offsetof(FmSynthParams, voices), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& fmSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(
            kParams,
            {
            { "vol", "Volume", SliderFormat::Percent },
            { "harmratio", "Harm ratio", SliderFormat::Choice, { ".125", ".2", ".25", ".333", ".5", "1", "2", "3", "4", "8", "16" } },
            { "tweak", "Tweak", SliderFormat::Decimal },
            { "mod", "Mod index", SliderFormat::Decimal },
            { "harmratio2", "Harm ratio 2", SliderFormat::Choice, { ".125", ".2", ".25", ".333", ".5", "1", "2", "3", "4", "8", "16" } },
            { "tweak2", "Tweak 2", SliderFormat::Decimal },
            { "mod2", "Mod index 2", SliderFormat::Decimal },
            { "attack", "Attack", SliderFormat::Millis, {}, true },
            { "decay", "Decay", SliderFormat::Millis, {}, true },
            { "sustain", "Sustain", SliderFormat::Percent },
            { "release", "Release", SliderFormat::Millis, {}, true },
            { "modAttack", "Mod attack", SliderFormat::Millis, {}, true },
            { "modDecay", "Mod decay", SliderFormat::Millis, {}, true },
            { "modSustain", "Mod sustain", SliderFormat::Percent },
            { "voices", "Voices", SliderFormat::Integer },
            });
            return specs;
         }

         float wrapCycles(float phase)
         {
            return phase - std::floor(phase);
         }
      }

      float FmSynthModuleAdapter::harmonicRatio(int choice)
      {
         return kHarmonicRatios[std::max(0, std::min(kRatioCount - 1, choice))];
      }

      ParamTable FmSynthModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> FmSynthModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> FmSynthModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> FmSynthModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), fmSpecs(), 180.0f);
      }

      dsp::EnvelopeSettings FmSynthModuleAdapter::ampEnvelope(const FmSynthParams& params)
      {
         return { params.attack, params.decay, params.sustain, params.release };
      }

      void FmSynthModuleAdapter::startVoice(FmVoice& voice,
                                            const PolyVoiceSlot& slot,
                                            const FmSynthParams& params,
                                            float sampleRate) const
      {
         (void)slot;
         (void)params;
         (void)sampleRate;
         // Phases restart so a chord's attack is the same every time (and golden renders are stable).
         voice.carrierPhase = 0.0f;
         voice.modPhase = 0.0f;
         voice.modPhase2 = 0.0f;
         voice.modEnvelope.start(1.0f);
      }

      void FmSynthModuleAdapter::releaseVoice(FmVoice& voice, const PolyVoiceSlot& slot, const FmSynthParams& params) const
      {
         (void)slot;
         (void)params;
         voice.modEnvelope.stop();
      }

      bool FmSynthModuleAdapter::renderVoice(FmVoice& voice,
                                             const PolyVoiceSlot& slot,
                                             const FmSynthParams& params,
                                             float* const* out,
                                             int count,
                                             float sampleRate) const
      {
         voice.modEnvelope.prepare({ params.modAttack, params.modDecay, params.modSustain, params.release }, sampleRate);
         const float inverseRate = 1.0f / sampleRate;
         const float nyquist = sampleRate * 0.5f;
         const float carrierHz = slot.frequency;
         const float harmHz = carrierHz * harmonicRatio(params.harmRatio) * params.tweak;
         const float harmHz2 = harmHz * harmonicRatio(params.harmRatio2) * params.tweak2;
         const float gain = params.vol * kVoiceGain;
         float* dst = out[0];
         for (int i = 0; i < count; ++i)
         {
            const float modIndex = params.mod * voice.modEnvelope.next();
            // Frequency modulation as in FMVoice::Process: each stage offsets the next one's frequency.
            voice.modPhase2 = wrapCycles(voice.modPhase2 + harmHz2 * inverseRate);
            const float modHz = harmHz + dsp::SineTable::at(voice.modPhase2) * harmHz2 * params.mod2;
            voice.modPhase = wrapCycles(voice.modPhase + std::max(-nyquist, std::min(nyquist, modHz)) * inverseRate);
            const float oscHz = carrierHz + dsp::SineTable::at(voice.modPhase) * harmHz * modIndex;
            voice.carrierPhase = wrapCycles(voice.carrierPhase + std::max(-nyquist, std::min(nyquist, oscHz)) * inverseRate);
            dst[i] = dsp::SineTable::at(voice.carrierPhase) * gain;
         }
         return true;
      }

      BESPOKE_REGISTER_MODULE(FmSynthModuleAdapter);

   } // namespace wasm
} // namespace bespoke
