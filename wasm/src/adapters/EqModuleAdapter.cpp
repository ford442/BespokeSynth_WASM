/**
 * BespokeSynth WASM - 4-band parametric EQ adapter implementation
 */

#include "BespokeWasm/adapters/EqModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
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
         constexpr float kShelfQ = 0.7071f;
         // Below this a band is flat to within rounding; skipping it saves the filter and any state drift.
         constexpr float kBypassDb = 0.01f;

         const std::vector<SliderSpec>& eqSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "lowFreq", "Low", 20.0f, 1000.0f, 100.0f, SliderFormat::Hertz, {}, true },
               { "lowGain", "Low gain", -18.0f, 18.0f, 0.0f, SliderFormat::Decibels },
               { "midFreq", "Mid", 80.0f, 8000.0f, 500.0f, SliderFormat::Hertz, {}, true },
               { "midGain", "Mid gain", -18.0f, 18.0f, 0.0f, SliderFormat::Decibels },
               { "midQ", "Mid Q", 0.3f, 8.0f, 1.0f, SliderFormat::Decimal },
               { "hiMidFreq", "Hi-mid", 400.0f, 16000.0f, 3000.0f, SliderFormat::Hertz, {}, true },
               { "hiMidGain", "Hi-mid gain", -18.0f, 18.0f, 0.0f, SliderFormat::Decibels },
               { "hiMidQ", "Hi-mid Q", 0.3f, 8.0f, 1.0f, SliderFormat::Decimal },
               { "highFreq", "High", 1000.0f, 18000.0f, 8000.0f, SliderFormat::Hertz, {}, true },
               { "highGain", "High gain", -18.0f, 18.0f, 0.0f, SliderFormat::Decibels },
               { "output", "Output", -18.0f, 18.0f, 0.0f, SliderFormat::Decibels },
            };
            return specs;
         }

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

      std::vector<WasmControlDescriptor> EqModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(eqSpecs());
      }

      std::vector<PortDescriptor> EqModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" } };
      }

      std::vector<PortDescriptor> EqModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> EqModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), eqSpecs(), 180.0f);
      }

      void EqModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         const auto& specs = eqSpecs();
         auto* params = new (dst) EqParams();
         params->band[0] = { sliderSpecValue(specs, controls, "lowFreq"), sliderSpecValue(specs, controls, "lowGain"), kShelfQ };
         params->band[1] = { sliderSpecValue(specs, controls, "midFreq"), sliderSpecValue(specs, controls, "midGain"),
                             sliderSpecValue(specs, controls, "midQ") };
         params->band[2] = { sliderSpecValue(specs, controls, "hiMidFreq"), sliderSpecValue(specs, controls, "hiMidGain"),
                             sliderSpecValue(specs, controls, "hiMidQ") };
         params->band[3] = { sliderSpecValue(specs, controls, "highFreq"), sliderSpecValue(specs, controls, "highGain"), kShelfQ };
         params->outputDb = sliderSpecValue(specs, controls, "output");
      }

      void EqModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         auto* state = new (runtimeState) EqRuntimeState();
         for (int band = 0; band < kEqBands; ++band)
         {
            for (auto& filter : state->filter[band])
               filter.SetFilterType(bandType(band));
         }
      }

      void EqModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<EqRuntimeState*>(runtimeState)->~EqRuntimeState();
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
