/**
 * BespokeSynth WASM - FFT channel vocoder adapter implementation
 */

#include "BespokeWasm/adapters/VocoderModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         using State = VocoderAdapterRuntimeState;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "input", .minValue = 0.0f, .maxValue = 4.0f, .defaultValue = 1.0f, .offset = offsetof(VocoderParams, input), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 1, .name = "carrier", .minValue = 0.0f, .maxValue = 4.0f, .defaultValue = 1.0f, .offset = offsetof(VocoderParams, carrier), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 2, .name = "volume", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 1.0f, .offset = offsetof(VocoderParams, volume), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 3, .name = "mix", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(VocoderParams, mix), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 4, .name = "bands", .minValue = 4.0f, .maxValue = static_cast<float>(State::kMaxBands), .defaultValue = 24.0f, .kind = ParamKind::IntRound, .offset = offsetof(VocoderParams, bands), .flags = kParamClamp },
            { .id = 5, .name = "smooth", .minValue = 0.0f, .maxValue = 0.95f, .defaultValue = 0.5f, .offset = offsetof(VocoderParams, smooth), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "Carrier" }, { PortType::Audio, "Modulator" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };

         // Hann analysis x Hann synthesis at 75 % overlap sums to 1.5.
         constexpr float kOverlapGain = 1.0f / 1.5f;

         const std::vector<SliderSpec>& vocoderSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "input", "Modulator", SliderFormat::Decimal },
                                                                           { "carrier", "Carrier", SliderFormat::Decimal },
                                                                           { "volume", "Volume", SliderFormat::Percent },
                                                                           { "mix", "Mix", SliderFormat::Percent },
                                                                           { "bands", "Bands", SliderFormat::Integer },
                                                                           { "smooth", "Smooth", SliderFormat::Percent },
                                                                           });
            return specs;
         }

         /** Mono mid of every cable into `port` at frame `i`. */
         float portSample(const WasmAudioProcessContext& context, int port, int i)
         {
            float sum = 0.0f;
            for (int c = 0; c < context.audioInputCount; ++c)
            {
               const WasmAudioInput& input = context.audioInputs[c];
               if (input.port != port || !input.channels[0])
                  continue;
               sum += input.numChannels == 2 ? 0.5f * (input.channels[0][i] + input.channels[1][i]) : input.channels[0][i];
            }
            return sum;
         }

         void layoutBands(State& state, int bands)
         {
            state.bandCount = bands;
            // Bin 1 (about 47 Hz at 48 kHz) up to Nyquist, log-spaced, each band at least one bin wide.
            const double top = static_cast<double>(State::kBins - 1);
            int previous = 0;
            for (int b = 0; b <= bands; ++b)
            {
               int edge = static_cast<int>(std::lround(std::pow(top, static_cast<double>(b) / bands)));
               edge = std::max(edge, previous + 1);
               state.bandStart[b] = std::min(edge, State::kBins);
               previous = state.bandStart[b];
            }
            state.bandStart[0] = 1;
            state.bandStart[bands] = State::kBins;
            std::fill(state.carrierEnvelope, state.carrierEnvelope + State::kMaxBands, 0.0f);
            std::fill(state.modulatorEnvelope, state.modulatorEnvelope + State::kMaxBands, 0.0f);
         }

         float binMagnitudeSquared(const float* spectrum, int bin)
         {
            if (bin == 0)
               return spectrum[0] * spectrum[0];
            if (bin == State::kBins - 1)
               return spectrum[1] * spectrum[1];
            return spectrum[2 * bin] * spectrum[2 * bin] + spectrum[2 * bin + 1] * spectrum[2 * bin + 1];
         }

         void processFrame(State& state, const VocoderParams& params)
         {
            float* carrierTime = state.carrierFft.timeBuffer();
            float* modulatorTime = state.modulatorFft.timeBuffer();
            for (int i = 0; i < State::kFrame; ++i)
            {
               carrierTime[i] = state.carrierHistory[i] * state.window[i];
               modulatorTime[i] = state.modulatorHistory[i] * state.window[i];
            }
            state.carrierFft.forward();
            state.modulatorFft.forward();
            float* carrier = state.carrierFft.spectrumBuffer();
            const float* modulator = state.modulatorFft.spectrumBuffer();

            const int bands = state.bandCount;
            const float keep = params.smooth;
            float bandGain[State::kMaxBands];
            for (int b = 0; b < bands; ++b)
            {
               double carrierEnergy = 0.0;
               double modulatorEnergy = 0.0;
               const int lo = state.bandStart[b];
               const int hi = state.bandStart[b + 1];
               for (int k = lo; k < hi; ++k)
               {
                  carrierEnergy += binMagnitudeSquared(carrier, k);
                  modulatorEnergy += binMagnitudeSquared(modulator, k);
               }
               const float width = static_cast<float>(std::max(1, hi - lo));
               const float carrierRms = std::sqrt(static_cast<float>(carrierEnergy) / width);
               const float modulatorRms = std::sqrt(static_cast<float>(modulatorEnergy) / width);
               state.carrierEnvelope[b] = keep * state.carrierEnvelope[b] + (1.0f - keep) * carrierRms;
               state.modulatorEnvelope[b] = keep * state.modulatorEnvelope[b] + (1.0f - keep) * modulatorRms;
               // The floor keeps silent carrier bands from blowing up into noise.
               constexpr float kFloor = 1.0e-3f;
               bandGain[b] = state.modulatorEnvelope[b] / std::max(kFloor, state.carrierEnvelope[b]);
            }

            // Per-bin gain, interpolated between band centres (no steps at band edges).
            for (int b = 0; b < bands; ++b)
            {
               const int lo = state.bandStart[b];
               const int hi = state.bandStart[b + 1];
               const float centre = 0.5f * static_cast<float>(lo + hi - 1);
               const int next = std::min(bands - 1, b + 1);
               const float nextCentre = 0.5f * static_cast<float>(state.bandStart[next] + state.bandStart[next + 1] - 1);
               for (int k = lo; k < hi; ++k)
               {
                  const float pos = static_cast<float>(k);
                  if (pos <= centre || next == b)
                  {
                     const int prev = std::max(0, b - 1);
                     const float prevCentre = 0.5f * static_cast<float>(state.bandStart[prev] + state.bandStart[prev + 1] - 1);
                     const float span = centre - prevCentre;
                     const float t = span > 0.0f ? std::max(0.0f, std::min(1.0f, (pos - prevCentre) / span)) : 1.0f;
                     state.binGain[k] = bandGain[prev] + (bandGain[b] - bandGain[prev]) * t;
                  }
                  else
                  {
                     const float span = nextCentre - centre;
                     const float t = span > 0.0f ? std::max(0.0f, std::min(1.0f, (pos - centre) / span)) : 0.0f;
                     state.binGain[k] = bandGain[b] + (bandGain[next] - bandGain[b]) * t;
                  }
               }
            }
            state.binGain[0] = 0.0f;

            carrier[0] *= state.binGain[0];
            carrier[1] *= state.binGain[State::kBins - 1];
            for (int k = 1; k < State::kBins - 1; ++k)
            {
               carrier[2 * k] *= state.binGain[k];
               carrier[2 * k + 1] *= state.binGain[k];
            }
            state.carrierFft.inverse();

            const float* wet = state.carrierFft.timeBuffer();
            for (int i = 0; i < State::kFrame; ++i)
               state.overlap[i] += wet[i] * state.window[i] * kOverlapGain;
            std::memcpy(state.outFifo, state.overlap, sizeof(float) * State::kHop);
            std::memmove(state.overlap, state.overlap + State::kHop, sizeof(float) * (State::kFrame - State::kHop));
            std::fill(state.overlap + State::kFrame - State::kHop, state.overlap + State::kFrame, 0.0f);
            std::memmove(state.carrierHistory, state.carrierHistory + State::kHop, sizeof(float) * (State::kFrame - State::kHop));
            std::memmove(state.modulatorHistory, state.modulatorHistory + State::kHop,
                         sizeof(float) * (State::kFrame - State::kHop));
         }
      }

      VocoderAdapterRuntimeState::VocoderAdapterRuntimeState()
      {
         for (int i = 0; i < kFrame; ++i)
            window[i] = 0.5f - 0.5f * static_cast<float>(std::cos(6.283185307179586 * i / kFrame));
      }

      int VocoderModuleAdapter::outputChannelCount(const void* params, const WasmAudioProcessContext& context) const
      {
         (void)params;
         (void)context;
         return 1;
      }

      ParamTable VocoderModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> VocoderModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> VocoderModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> VocoderModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), vocoderSpecs(), 160.0f);
      }

      void VocoderModuleAdapter::processAudio(void* runtimeState,
                                              const void* paramsPtr,
                                              float* const* channels,
                                              const WasmAudioProcessContext& context) const
      {
         float* out = channels ? channels[0] : nullptr;
         if (!runtimeState || !paramsPtr || !out)
            return;
         auto& state = *static_cast<State*>(runtimeState);
         const auto& params = *static_cast<const VocoderParams*>(paramsPtr);
         if (!state.carrierFft.valid() || !state.modulatorFft.valid())
         {
            std::fill(out, out + context.numSamples, 0.0f);
            return;
         }
         if (state.bandCount != params.bands)
            layoutBands(state, std::max(1, std::min(State::kMaxBands, params.bands)));

         const float wetGain = params.mix * params.volume;
         const float dryGain = 1.0f - params.mix;
         for (int i = 0; i < context.numSamples; ++i)
         {
            const float carrier = portSample(context, 0, i) * params.carrier;
            const float modulator = portSample(context, 1, i) * params.input;
            const int at = State::kFrame - State::kHop + state.hopFill;
            state.carrierHistory[at] = carrier;
            state.modulatorHistory[at] = modulator;
            out[i] = state.outFifo[state.hopFill] * wetGain + carrier * dryGain;
            if (++state.hopFill == State::kHop)
            {
               state.hopFill = 0;
               processFrame(state, params);
            }
         }
      }

      BESPOKE_REGISTER_MODULE(VocoderModuleAdapter);

   } // namespace wasm
} // namespace bespoke
