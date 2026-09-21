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

#include "BespokeWasm/WasmModuleAdapter.h"

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

      class CompressorModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "compressor"; }
         const char* displayName() const override { return "Compressor"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }
         bool sumsAudioInputs() const override { return false; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(CompressorParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(CompressorRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;

         /** Static gain curve (dB change for a given input level), exposed for tests. */
         static float gainComputerDb(float levelDb, const CompressorParams& params);
      };

   } // namespace wasm
} // namespace bespoke
