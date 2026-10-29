/**
 * BespokeSynth WASM - FFT channel vocoder adapter
 *
 * Two inputs: Carrier (port 0, e.g. a synth) and Modulator (port 1, e.g. the mic).
 * A 1024-point STFT with 75 % overlap (Hann analysis and synthesis, dsp::RealFft on
 * PFFFT) measures both spectra in `bands` log-spaced bands and scales the carrier so
 * its spectral envelope follows the modulator's; the carrier keeps its pitch. Latency
 * is one frame (1024 samples). Reference only: Source/FFTVocoder.cpp crosses whole
 * spectra and Source/BandVocoder.cpp runs a filter bank; both take the carrier from a
 * separate `vocodercarrier` module and are not compiled.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include "BespokeWasm/dsp/Fft.h"

namespace bespoke
{
   namespace wasm
   {

      struct VocoderParams
      {
         float input = 1.0f;
         float carrier = 1.0f;
         float volume = 1.0f;
         float mix = 1.0f;
         int bands = 24;
         float smooth = 0.5f;
      };

      struct VocoderAdapterRuntimeState
      {
         static constexpr int kFrame = 1024;
         static constexpr int kHop = 256;
         static constexpr int kBins = kFrame / 2 + 1;
         static constexpr int kMaxBands = 64;

         VocoderAdapterRuntimeState();

         dsp::RealFft carrierFft{ kFrame };
         dsp::RealFft modulatorFft{ kFrame };
         float window[kFrame]{};
         float carrierHistory[kFrame]{};
         float modulatorHistory[kFrame]{};
         float overlap[kFrame]{};
         float outFifo[kHop]{};
         int hopFill = 0;

         int bandCount = 0;
         int bandStart[kMaxBands + 1]{};
         float carrierEnvelope[kMaxBands]{};
         float modulatorEnvelope[kMaxBands]{};
         float binGain[kBins]{};
      };

      class VocoderModuleAdapter : public AdapterStorage<VocoderParams, VocoderAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "vocoder"; }
         const char* displayName() const override { return "Vocoder"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
         bool sumsAudioInputs() const override { return false; }
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override;

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         static constexpr int kLatencySamples = VocoderAdapterRuntimeState::kFrame;
      };

   } // namespace wasm
} // namespace bespoke
