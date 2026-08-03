/**
 * BespokeSynth WASM - Step sequencer adapter
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

      struct StepSequencerRuntimeState
      {
         int lastEmittedStep = -1;
         bool noteIsOn = false;
         int activePitch = 60;
         double noteOffBeat = -1.0;
      };

      class StepSequencerModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "stepsequencer"; }
         const char* displayName() const override { return "Step Sequencer"; }
         ModuleCategory category() const override { return ModuleCategory::Pulse; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteSource; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;
         void fillAudioGraphNode(const std::map<std::string, float>& controls,
                                 AudioGraphNode& node) const override;

         size_t runtimeStateSize() const override { return sizeof(StepSequencerRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;

         /** Emit note on/off events for steps crossed in [beatStart, beatEnd). */
         void emitNotesForBeatRange(void* runtimeState,
                                    const AudioGraphNode& node,
                                    double beatStart,
                                    double beatEnd,
                                    std::vector<WasmNoteEvent>& outNotes) const;
      };

   } // namespace wasm
} // namespace bespoke
