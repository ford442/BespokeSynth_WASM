/**
 * BespokeSynth WASM - Note-triggered sampler adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include "BespokeWasm/SampleBuffer.h"
#include "BespokeWasm/SampleStore.h"

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
         /** `handle` resolved when the params were filled; pinned by the snapshot (collectResources). */
         const SampleBuffer* sample = nullptr;
         /** The buffer in the SampleStore; rebindParams resolves it in another heap. */
         SampleHandle handle;
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

      class SamplerModuleAdapter : public AdapterStorage<SamplerParams, SamplerAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "sampler"; }
         const char* displayName() const override { return "Sampler"; }
         ModuleCategory category() const override { return ModuleCategory::Synth; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioSource; }
         /** Stereo files play stereo; mono files (or no sample) stay mono. */
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override;

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         /** Resolves extras["sampleHash"] to `sample` / `sampleId`. */
         void bindParams(int moduleId, const WasmStringMap& extras, void* params) const override;
         /** Keeps end after start and loopEnd after loopStart. */
         void finalizeParams(void* params) const override;
         void rebindParams(int moduleId, void* params) const override;
         void collectResources(int moduleId, const void* params, WasmResourceRefs& refs) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
