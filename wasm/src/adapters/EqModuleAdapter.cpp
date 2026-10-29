/**
 * BespokeSynth WASM - 4-band parametric EQ adapter implementation
 */

#include "BespokeWasm/adapters/EqModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/SliderPanelModule.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/WidgetPainter.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr float kShelfQ = 0.7071f;
         // Below this a band is flat to within rounding; skipping it saves the filter and any state drift.
         constexpr float kBypassDb = 0.01f;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "lowFreq", .minValue = 20.0f, .maxValue = 1000.0f, .defaultValue = 100.0f, .offset = offsetof(EqParams, band[0].freq), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "lowGain", .minValue = -18.0f, .maxValue = 18.0f, .defaultValue = 0.0f, .offset = offsetof(EqParams, band[0].gainDb), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 2, .name = "midFreq", .minValue = 80.0f, .maxValue = 8000.0f, .defaultValue = 500.0f, .offset = offsetof(EqParams, band[1].freq), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 3, .name = "midGain", .minValue = -18.0f, .maxValue = 18.0f, .defaultValue = 0.0f, .offset = offsetof(EqParams, band[1].gainDb), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 4, .name = "midQ", .minValue = 0.3f, .maxValue = 8.0f, .defaultValue = 1.0f, .offset = offsetof(EqParams, band[1].q), .flags = kParamClamp },
            { .id = 5, .name = "hiMidFreq", .minValue = 400.0f, .maxValue = 16000.0f, .defaultValue = 3000.0f, .offset = offsetof(EqParams, band[2].freq), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 6, .name = "hiMidGain", .minValue = -18.0f, .maxValue = 18.0f, .defaultValue = 0.0f, .offset = offsetof(EqParams, band[2].gainDb), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 7, .name = "hiMidQ", .minValue = 0.3f, .maxValue = 8.0f, .defaultValue = 1.0f, .offset = offsetof(EqParams, band[2].q), .flags = kParamClamp },
            { .id = 8, .name = "highFreq", .minValue = 1000.0f, .maxValue = 18000.0f, .defaultValue = 8000.0f, .offset = offsetof(EqParams, band[3].freq), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 9, .name = "highGain", .minValue = -18.0f, .maxValue = 18.0f, .defaultValue = 0.0f, .offset = offsetof(EqParams, band[3].gainDb), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 10, .name = "output", .minValue = -18.0f, .maxValue = 18.0f, .defaultValue = 0.0f, .offset = offsetof(EqParams, outputDb), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         const std::vector<SliderSpec>& eqSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "lowFreq", "Low", SliderFormat::Hertz, {}, true },
                                                                           { "lowGain", "Low gain", SliderFormat::Decibels },
                                                                           { "midFreq", "Mid", SliderFormat::Hertz, {}, true },
                                                                           { "midGain", "Mid gain", SliderFormat::Decibels },
                                                                           { "midQ", "Mid Q", SliderFormat::Decimal },
                                                                           { "hiMidFreq", "Hi-mid", SliderFormat::Hertz, {}, true },
                                                                           { "hiMidGain", "Hi-mid gain", SliderFormat::Decibels },
                                                                           { "hiMidQ", "Hi-mid Q", SliderFormat::Decimal },
                                                                           { "highFreq", "High", SliderFormat::Hertz, {}, true },
                                                                           { "highGain", "High gain", SliderFormat::Decibels },
                                                                           { "output", "Output", SliderFormat::Decibels },
                                                                           });
            return specs;
         }

         /** Sliders plus the live response curve of the current settings. */
         class EqModule : public SliderPanelModule
         {
         public:
            static constexpr float kCurveHeight = 56.0f;
            static constexpr int kPoints = 96;
            static constexpr float kRangeDb = 18.0f;
            // The canvas has no device rate; 48 kHz only moves the curve near Nyquist.
            static constexpr float kDisplayRate = 48000.0f;

            explicit EqModule(int id)
            : SliderPanelModule(id, "eq", "EQ", ModuleCategory::AudioEffect, eqSpecs(), 180.0f, kCurveHeight + 6.0f)
            {
            }

            void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override
            {
               SliderPanelModule::render(renderer, offsetX, offsetY, scale);
               EqParams params;
               for (const ParamDescriptor& param : kParams)
                  writeParam(&params, param, getControlValue(param.name));
               float curve[kPoints];
               for (int i = 0; i < kPoints; ++i)
               {
                  // 20 Hz .. 20 kHz, log-spaced.
                  const float freq = 20.0f * std::pow(1000.0f, static_cast<float>(i) / static_cast<float>(kPoints - 1));
                  curve[i] = EqModuleAdapter::responseDb(params, freq, kDisplayRate);
               }
               const float x = (mX + offsetX + kSidePad) * scale;
               const float y = (mY + offsetY + slidersTop() - kCurveHeight - 6.0f) * scale;
               WidgetPainter(renderer).drawResponseCurve(x, y, (mWidth - 2.0f * kSidePad) * scale, kCurveHeight * scale,
                                                         curve, kPoints, kRangeDb, UITheme::kAccentCyan);
            }
         };

         void applyBand(EqRuntimeState& state, int band, const EqBandParams& wanted, float sampleRate)
         {
            const float freq = std::max(10.0f, std::min(wanted.freq, sampleRate * 0.45f));
            BiquadFilter& lead = state.filter[band][0];
            lead.SetSampleRate(sampleRate);
            lead.mF = freq;
            lead.mQ = EqModuleAdapter::bandType(band) == kFilterType_Peak ? wanted.q : kShelfQ;
            lead.mDbGain = wanted.gainDb;
            lead.UpdateFilterCoeff();
            BiquadFilter& follow = state.filter[band][1];
            follow.SetSampleRate(sampleRate);
            follow.mF = lead.mF;
            follow.mQ = lead.mQ;
            follow.mDbGain = lead.mDbGain;
            follow.CopyCoeffFrom(lead);
            state.applied[band] = wanted;
         }
      }

      float EqModuleAdapter::responseDb(const EqParams& params, float freq, float sampleRate)
      {
         float magnitude = 1.0f;
         for (int band = 0; band < kEqBands; ++band)
         {
            const EqBandParams& wanted = params.band[band];
            if (std::fabs(wanted.gainDb) < kBypassDb)
               continue;
            BiquadFilter filter;
            filter.SetSampleRate(sampleRate);
            filter.SetFilterType(bandType(band));
            filter.mF = std::max(10.0f, std::min(wanted.freq, sampleRate * 0.45f));
            filter.mQ = bandType(band) == kFilterType_Peak ? wanted.q : kShelfQ;
            filter.mDbGain = wanted.gainDb;
            filter.UpdateFilterCoeff();
            magnitude *= filter.GetMagnitudeResponseAt(freq);
         }
         return 20.0f * std::log10(std::max(1.0e-6f, magnitude)) + params.outputDb;
      }

      FilterType EqModuleAdapter::bandType(int band)
      {
         switch (band)
         {
            case 0:
               return kFilterType_LowShelf;
            case 3:
               return kFilterType_HighShelf;
            default:
               return kFilterType_Peak;
         }
      }

      EqRuntimeState::EqRuntimeState()
      {
         for (int band = 0; band < kEqBands; ++band)
         {
            for (auto& bandFilter : filter[band])
               bandFilter.SetFilterType(EqModuleAdapter::bandType(band));
         }
      }

      ParamTable EqModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> EqModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> EqModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> EqModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<EqModule>(id);
      }

      void EqModuleAdapter::finalizeParams(void* params) const
      {
         auto& eq = paramsOf(params);
         eq.band[0].q = kShelfQ;
         eq.band[3].q = kShelfQ;
      }

      void EqModuleAdapter::processAudio(void* runtimeState,
                                         const void* paramsPtr,
                                         float* const* channels,
                                         const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;

         auto& state = *static_cast<EqRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const EqParams*>(paramsPtr);
         const float sampleRate = std::max(1.0f, context.sampleRate);
         const bool rateChanged = state.lastSampleRate != sampleRate;

         for (int band = 0; band < kEqBands; ++band)
         {
            const EqBandParams& wanted = params.band[band];
            const EqBandParams& applied = state.applied[band];
            const bool changed = !state.initialized || rateChanged || wanted.freq != applied.freq ||
                                 wanted.gainDb != applied.gainDb || wanted.q != applied.q;
            if (changed)
               applyBand(state, band, wanted, sampleRate);
         }
         state.initialized = true;
         state.lastSampleRate = sampleRate;

         for (int band = 0; band < kEqBands; ++band)
         {
            const bool flat = std::fabs(params.band[band].gainDb) < kBypassDb;
            for (int ch = 0; ch < context.numChannels; ++ch)
            {
               if (flat)
                  state.filter[band][ch].Clear();
               else
                  state.filter[band][ch].Filter(channels[ch], context.numSamples);
            }
         }

         if (std::fabs(params.outputDb) >= kBypassDb)
         {
            const float gain = std::pow(10.0f, params.outputDb / 20.0f);
            for (int ch = 0; ch < context.numChannels; ++ch)
               dsp::scale(channels[ch], context.numSamples, gain);
         }
      }

      BESPOKE_REGISTER_MODULE(EqModuleAdapter);

   } // namespace wasm
} // namespace bespoke
