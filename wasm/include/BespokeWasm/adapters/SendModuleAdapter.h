/**
 * BespokeSynth WASM - Audio send adapter
 *
 * Desktop AudioSend: the input goes out of "Out" and, scaled by `amount`, out of
 * "Send". Patch several sends into one reverb or delay for an aux bus without a
 * mixer. With `crossfade`, Out fades down as the send comes up. The second Audio
 * output is a plan aux slot (WasmAudioProcessContext::auxOutputs).
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

      struct SendParams
      {
         float amount = 0.5f;
         bool crossfade = false;
      };

      class SendModuleAdapter : public AdapterStorage<SendParams>
      {
      public:
         const char* typeId() const override { return "send"; }
         const char* displayName() const override { return "Send"; }
         ModuleCategory category() const override { return ModuleCategory::AudioEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioProcessor; }

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
