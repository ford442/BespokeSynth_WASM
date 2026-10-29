/**
 * BespokeSynth WASM - Note step sequencer adapter
 *
 * Up to 16 steps, each with a tone (a grid row), a velocity (0 = rest) and a length
 * (fraction of the step), edited on a pitch/velocity/length grid widget
 * (NoteSequencerModule). Rows are degrees of the graph Scale above `octave`, or
 * semitones in chromatic mode. It emits on the transport grid like the arpeggiator,
 * so it is a NoteProcessor (upstream-first, with the Scale in its block) that ignores
 * input notes and has no Note input. Reference only: Source/NoteStepSequencer.cpp is a
 * drawable module and is not compiled.
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

      constexpr int kNoteSeqMaxSteps = 16;

      struct NoteSequencerParams
      {
         int steps = 8;
         int interval = 2;
         int octave = 4;
         int rows = 8;
         int mode = 0;
         float tone[kNoteSeqMaxSteps]{};
         float velocity[kNoteSeqMaxSteps]{};
         float length[kNoteSeqMaxSteps]{};
      };

      struct NoteSequencerRuntimeState
      {
         long long lastStep = -1;
         bool sounding = false;
         int soundingPitch = 60;
         double noteOffBeat = -1.0;
      };

      class NoteSequencerModuleAdapter : public AdapterStorage<NoteSequencerParams, NoteSequencerRuntimeState>
      {
      public:
         const char* typeId() const override { return "notesequencer"; }
         const char* displayName() const override { return "Note Sequencer"; }
         ModuleCategory category() const override { return ModuleCategory::Pulse; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteProcessor; }
         // Rows are already scale degrees.
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

         /** Beats per step for an `interval` choice (1/4, 1/8, 1/16, 1/32). */
         static double stepBeats(int interval);
         /** The MIDI pitch of grid row `tone`. */
         static int rowPitch(const NoteSequencerParams& params, int tone, const GraphMusicScale* scale);
      };

   } // namespace wasm
} // namespace bespoke
