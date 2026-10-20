/**
 * BespokeSynth WASM - Envelope modulator adapter
 *
 * An ADSR (dsp::Envelope) as a modulation source: note-on starts it, the last
 * note-off releases it, a pulse fires a one-shot (attack, decay, then release).
 * The output spans `low`..`high` in modulation units (-1..1). With `useVelocity`
 * the peak follows note velocity / pulse strength. Reference only:
 * Source/EnvelopeModulator.cpp is a drawable module and is not compiled.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include "BespokeWasm/dsp/Envelope.h"

namespace bespoke
{
   namespace wasm
   {

      struct EnvelopeParams
      {
         float attack = 10.0f;
         float decay = 200.0f;
         float sustain = 0.5f;
         float release = 300.0f;
         float low = 0.0f;
         float high = 1.0f;
         bool useVelocity = false;
      };

      struct EnvelopeAdapterRuntimeState
      {
         dsp::Envelope envelope;
         int heldNotes = 0;
         bool held[128]{};
         /** A pulse-started envelope releases on its own once it reaches sustain. */
         bool oneShot = false;
      };

      class EnvelopeModuleAdapter : public AdapterStorage<EnvelopeParams, EnvelopeAdapterRuntimeState>
      {
      public:
         const char* typeId() const override { return "envelope"; }
         const char* displayName() const override { return "Envelope"; }
         ModuleCategory category() const override { return ModuleCategory::Modulator; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::ModulationSource; }

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
