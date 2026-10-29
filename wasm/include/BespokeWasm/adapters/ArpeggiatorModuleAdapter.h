/**
 * BespokeSynth WASM - Arpeggiator (Note processor) adapter
 *
 * Holds the notes it is fed and plays them one at a time on the transport grid
 * (steps fire on beat multiples, so the pattern stays locked to the Transport
 * and to any sequencer that shares it). Reference only: Source/Arpeggiator.cpp
 * is a drawable module and is not compiled.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include <cstdint>

namespace bespoke
{
   namespace wasm
   {

      constexpr int kArpMaxHeld = 16;

      enum class ArpMode
      {
         Up = 0,
         Down = 1,
         UpDown = 2,
         Random = 3,
         AsPlayed = 4
      };

      struct ArpeggiatorParams
      {
         int rate = 2; // 0 = 1/4, 1 = 1/8, 2 = 1/16, 3 = 1/32
         int mode = 0;
         int octaves = 1;
         float gate = 0.6f; // fraction of a step
      };

      struct ArpeggiatorRuntimeState
      {
         struct Held
         {
            uint8_t pitch = 0;
            uint8_t channel = 0;
            float velocity = 0.0f;
         };
         Held held[kArpMaxHeld]{}; // press order
         int heldCount = 0;
         long long lastStep = -1;
         int position = 0; // index into the expanded pattern
         bool sounding = false;
         int soundingPitch = 0;
         int soundingChannel = 0;
         double noteOffBeat = -1.0;
         uint32_t rng = 0x2545F491u;
      };

      class ArpeggiatorModuleAdapter : public AdapterStorage<ArpeggiatorParams, ArpeggiatorRuntimeState>
      {
      public:
         const char* typeId() const override { return "arpeggiator"; }
         const char* displayName() const override { return "Arpeggiator"; }
         ModuleCategory category() const override { return ModuleCategory::NoteEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteProcessor; }
         bool quantizesNotesToScale() const override { return false; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void processNotes(void* runtimeState,
                           const void* params,
                           const WasmNoteProcessBlock& block,
                           WasmNoteEvent* outNotes,
                           int maxNotes,
                           int& outCount) const override;

         /** Beats per step for a `rate` choice. */
         static double stepBeats(int rate);
      };

   } // namespace wasm
} // namespace bespoke
