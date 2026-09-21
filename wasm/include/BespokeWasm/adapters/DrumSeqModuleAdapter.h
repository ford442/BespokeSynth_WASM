/**
 * BespokeSynth WASM - Four-track drum step sequencer adapter
 *
 * NoteSource driven by Pulse: every incoming pulse advances one step. Each
 * track has its own Note output (WasmNoteEvent::outputPort = track), so kick,
 * snare, hat and tom can feed different instruments.
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

      constexpr int kDrumSeqTracks = 4;
      constexpr int kDrumSeqMaxSteps = 16;

      struct DrumSeqParams
      {
         int patternMask[kDrumSeqTracks] = { 0x1111, 0x1010, 0x4444, 0x0000 };
         int pitch[kDrumSeqTracks] = { 36, 38, 42, 45 };
         int steps = kDrumSeqMaxSteps;
         float gate = 0.5f;
      };

      struct DrumSeqRuntimeState
      {
         long long stepCounter = 0;
         bool noteIsOn[kDrumSeqTracks]{};
         int activePitch[kDrumSeqTracks]{};
         double noteOffBeat[kDrumSeqTracks] = { -1.0, -1.0, -1.0, -1.0 };
      };

      class DrumSeqModule : public Module
      {
      public:
         DrumSeqModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         bool handleMouseDown(float worldX, float worldY) override;

         bool isStepActive(int track, int step) const;
         void setStepActive(int track, int step, bool active);

      private:
         float gridTop() const;

         int mPatternMask[kDrumSeqTracks] = { 0x1111, 0x1010, 0x4444, 0x0000 };
         int mPitch[kDrumSeqTracks] = { 36, 38, 42, 45 };
         int mSteps = kDrumSeqMaxSteps;
         float mGate = 0.5f;
      };

      class DrumSeqModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "drumseq"; }
         const char* displayName() const override { return "Drum Sequencer"; }
         ModuleCategory category() const override { return ModuleCategory::Pulse; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteSource; }
         // Drum pitches are trigger ids, not melody: never snap them to the Scale.
         bool quantizesNotesToScale() const override { return false; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(DrumSeqParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(DrumSeqRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void emitNotesForBeatRange(void* runtimeState,
                                    const void* params,
                                    double beatStart,
                                    double beatEnd,
                                    WasmNoteEvent* outNotes,
                                    int maxNotes,
                                    int& outCount,
                                    const WasmPulseEvent* pulses,
                                    int pulseCount) const override;
      };

   } // namespace wasm
} // namespace bespoke
