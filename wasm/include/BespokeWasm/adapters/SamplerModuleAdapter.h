/**
 * BespokeSynth WASM - Note-triggered sampler adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/SampleBuffer.h"
#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      constexpr int kSamplerVoiceCount = 8;

      enum class SamplerPlayMode
      {
         OneShot = 0,
         Loop = 1,
         Gate = 2
      };

      struct SamplerParams
      {
         const SampleBuffer* sample = nullptr;
         float volume = 0.85f;
         float start = 0.0f;
         float end = 1.0f;
         float loopStart = 0.0f;
         float loopEnd = 1.0f;
         int mode = 0;
         int rootPitch = 60;
      };

      struct SamplerVoice
      {
         bool active = false;
         bool gated = false;
         int pitch = 60;
         float velocity = 1.0f;
         double position = 0.0;
         float rate = 1.0f;
         uint32_t age = 0;
      };

      struct SamplerAdapterRuntimeState
      {
         SamplerVoice voices[kSamplerVoiceCount]{};
         uint32_t ageCounter = 0;
      };

      class SamplerModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "sampler"; }
         const char* displayName() const override { return "Sampler"; }
         ModuleCategory category() const override { return ModuleCategory::Synth; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioSource; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(SamplerParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;
         void fillParams(int moduleId,
                         const WasmControlMap& controls,
                         const WasmStringMap& extras,
                         void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(SamplerAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
