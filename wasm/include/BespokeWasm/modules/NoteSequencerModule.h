/**
 * BespokeSynth WASM - Note step sequencer canvas UI (pitch / velocity / length grid)
 *
 * The grid shows `rows` tones by `steps` steps: click a cell to set that step's tone
 * (click the lit cell again to rest it). Under it, drag the velocity and length lanes.
 * Global controls (steps, interval, octave, rows, mode) are the slider rows below.
 * Per-step values are ordinary controls (tone0..15, vel0..15, len0..15), so the
 * canvas syncs, saves and undoes them like any slider.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/SliderPanelModule.h"
#include "BespokeWasm/adapters/NoteSequencerModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {

      class NoteSequencerModule : public SliderPanelModule
      {
      public:
         static constexpr float kCellWidth = 12.0f;
         static constexpr float kGridHeight = 96.0f;
         static constexpr float kVelocityLaneHeight = 24.0f;
         static constexpr float kLengthLaneHeight = 14.0f;
         static constexpr float kLaneGap = 4.0f;

         NoteSequencerModule(int id, ParamTable params);

         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         bool handleMouseDown(float worldX, float worldY) override;
         bool handleMouseDrag(float worldX, float worldY, float dx, float dy) override;
         void handleMouseUp() override;
         std::string controlNameAt(float worldX, float worldY) const override;
         bool controlRange(const std::string& name, float& minValue, float& maxValue) const override;

      private:
         enum class Lane
         {
            None,
            Grid,
            Velocity,
            Length
         };

         struct Hit
         {
            Lane lane = Lane::None;
            int step = -1;
            int row = -1;
            float value = 0.0f; // 0..1 up the velocity / length lane
         };

         Hit hitAt(float worldX, float worldY) const;
         float gridTop() const;
         int stepCount() const;
         int rowCount() const;
         /** Parses "tone3" / "vel12" / "len0"; -1 when `name` is not a step control. */
         static int stepControl(const std::string& name, char& kind);

         float mTone[kNoteSeqMaxSteps]{};
         float mVelocity[kNoteSeqMaxSteps]{};
         float mLength[kNoteSeqMaxSteps]{};
         Lane mDragLane = Lane::None;
      };

   } // namespace wasm
} // namespace bespoke
