/**
 * BespokeSynth WASM - Schroeder reverb adapter
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/Module.h"
#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      struct ReverbParams
      {
         float roomSize = 0.7f;
         float damping = 0.3f;
         float mix = 0.35f;
      };

      struct ReverbAdapterRuntimeState
      {
         static constexpr int kCombCount = 4;
         static constexpr int kAllpassCount = 2;
         static constexpr int kMaxComb = 2048;
         static constexpr int kMaxAllpass = 612;

         float comb[kCombCount][kMaxComb]{};
         int combIndex[kCombCount]{};
         float combFilter[kCombCount]{};
         int combSize[kCombCount]{};
         float allpass[kAllpassCount][kMaxAllpass]{};
         int allpassIndex[kAllpassCount]{};
         int allpassSize[kAllpassCount]{};
         float lastSampleRate = 0.0f;
      };

      class ReverbModule : public Module
      {
      public:
         ReverbModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;

      private:
         float mRoomSize = 0.7f;
         float mDamping = 0.3f;
         float mMix = 0.35f;
      };

      class ReverbModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "reverb"; }
         const char* displayName() const override { return "Reverb"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(ReverbParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(ReverbAdapterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* buffer,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
