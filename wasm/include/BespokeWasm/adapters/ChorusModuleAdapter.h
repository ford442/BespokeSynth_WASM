/**
 * BespokeSynth WASM - Two-voice modulated-delay chorus adapter
 *
 * In-adapter kernel (juce_dsp::Chorus bodies live in juce_dsp.cpp). The ring
 * buffer is fixed-size inside the runtime state, so no allocation happens on
 * the audio thread.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      struct ChorusParams
      {
         float rateHz = 0.8f;
         float depth = 0.5f;
         float delayMs = 15.0f;
         float feedback = 0.0f;
         float mix = 0.5f;
         float width = 1.0f; // 0 = both voices on both sides (mono), 1 = voice A left, voice B right
      };

      struct ChorusRuntimeState
      {
         static constexpr int kRingSize = 4096; // power of two; > 40 ms at 96 kHz
         static constexpr int kVoices = 2;

         float ring[2][kRingSize]{}; // per channel; a mono block only uses ring[0]
         int writeIndex = 0;
         float lfoPhase = 0.0f; // 0..1
         float lastWet[2]{};
         int lastChannels = 1;
      };

      class ChorusModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "chorus"; }
         const char* displayName() const override { return "Chorus"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;
         /** Stereo when the input is stereo or width > 0; a mono input at width 0 stays mono. */
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override;

         size_t paramsSize() const override { return sizeof(ChorusParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(ChorusRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
