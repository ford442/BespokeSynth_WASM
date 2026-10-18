/**
 * BespokeSynth WASM - Reverb adapter (Jezar's Freeverb from libs/freeverb)
 *
 * The runtime state (a `revmodel`, ~110 KB of comb/allpass buffers) is private to
 * the adapter source so freeverb headers stay out of every other translation unit.
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
         /** Freeverb's decorrelated L/R tails are always kept. */
         int outputChannelCount(const void*, const WasmAudioProcessContext&) const override { return 2; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(ReverbParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override;
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
