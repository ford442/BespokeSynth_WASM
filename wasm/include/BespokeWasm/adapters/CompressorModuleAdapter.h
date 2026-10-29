/**
 * BespokeSynth WASM - Feed-forward compressor adapter
 *
 * In-adapter scalar kernel: juce_dsp::Compressor is not header-only (its
 * template bodies live in juce_dsp.cpp), and compiling the whole juce_dsp
 * module for one ballistics filter is not worth the binary size.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"

namespace bespoke
{
   namespace wasm
   {

      struct CompressorParams
      {
         float thresholdDb = -18.0f;
         float ratio = 4.0f;
         float attackMs = 5.0f;
         float releaseMs = 120.0f;
         float makeupDb = 0.0f;
         float kneeDb = 6.0f;
      };

      struct CompressorRuntimeState
      {
         float envelopeDb = 0.0f; // smoothed gain change (<= 0)
      };

      class CompressorModuleAdapter : public AdapterStorage<CompressorParams, CompressorRuntimeState>
      {
      public:
         const char* typeId() const override { return "compressor"; }
         const char* displayName() const override { return "Compressor"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
         bool sumsAudioInputs() const override { return false; }
         /** Width of the main input; the sidechain only keys the detector. */
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override;

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;

         /** Static gain curve (dB change for a given input level), exposed for tests. */
         static float gainComputerDb(float levelDb, const CompressorParams& params);
      };

   } // namespace wasm
} // namespace bespoke
