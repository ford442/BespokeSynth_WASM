/**
 * BespokeSynth WASM - Input (microphone) source adapter
 *
 * Plays the browser microphone bus (InputAudioBus, fed by the header Input button
 * through an AudioWorklet) into the graph. The bus is mono; a mono source reaches
 * a stereo bus as both channels, like every other mono source. Nothing is routed
 * to the Output implicitly: the signal only goes where the user cables it, so a
 * feedback loop through the speakers needs an explicit Input → … → Output cable.
 * Desktop reference: Source/InputChannel.cpp (drawable, not compiled).
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include "BespokeWasm/SliderPanelModule.h"

namespace bespoke
{
   namespace wasm
   {

      struct InputParams
      {
         float gain = 1.0f;
         bool mute = false;
      };

      /** Slider panel plus a capture status line and a live level bar. */
      class InputModule : public SliderPanelModule
      {
      public:
         InputModule(int id, std::vector<SliderSpec> specs);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;

      private:
         float mLevel = 0.0f;
      };

      class InputModuleAdapter : public AdapterStorage<InputParams>
      {
      public:
         const char* typeId() const override { return "input"; }
         const char* displayName() const override { return "Input"; }
         ModuleCategory category() const override { return ModuleCategory::Synth; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioSource; }
         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override
         {
            (void)params;
            (void)context;
            return 1;
         }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processAudio(void* runtimeState,
                           const void* params,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override;
      };

   } // namespace wasm
} // namespace bespoke
