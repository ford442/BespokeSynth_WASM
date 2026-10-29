/**
 * BespokeSynth WASM - Note step sequencer canvas UI
 */

#include "BespokeWasm/modules/NoteSequencerModule.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr float kHeaderHeight = NoteSequencerModule::kGridHeight + NoteSequencerModule::kLaneGap +
                                         NoteSequencerModule::kVelocityLaneHeight + NoteSequencerModule::kLaneGap +
                                         NoteSequencerModule::kLengthLaneHeight + 8.0f;
         constexpr float kPanelWidth = NoteSequencerModule::kCellWidth * kNoteSeqMaxSteps + 2.0f * SliderPanelModule::kSidePad;
         constexpr float kDefaultVelocity = 0.8f;

         std::vector<SliderSpec> globalSpecs(ParamTable params)
         {
            return slidersFromParams(params,
                                     {
                                     { "steps", "Steps", SliderFormat::Integer },
                                     { "interval", "Interval", SliderFormat::Choice, { "1/4", "1/8", "1/16", "1/32" } },
                                     { "octave", "Octave", SliderFormat::Integer },
                                     { "rows", "Rows", SliderFormat::Integer },
                                     { "mode", "Rows are", SliderFormat::Choice, { "scale", "chromatic" } },
                                     });
         }
      }

      NoteSequencerModule::NoteSequencerModule(int id, ParamTable params)
      : SliderPanelModule(id, "notesequencer", "Note Sequencer", ModuleCategory::Pulse, globalSpecs(params), kPanelWidth,
                          kHeaderHeight)
      {
         for (const ParamDescriptor& param : params)
         {
            char kind = 0;
            const int step = stepControl(param.name, kind);
            if (step < 0)
               continue;
            float* lane = kind == 't' ? mTone : (kind == 'v' ? mVelocity : mLength);
            lane[step] = param.defaultValue;
         }
      }

      int NoteSequencerModule::stepControl(const std::string& name, char& kind)
      {
         size_t prefix = 0;
         if (name.rfind("tone", 0) == 0)
            prefix = 4;
         else if (name.rfind("vel", 0) == 0 || name.rfind("len", 0) == 0)
            prefix = 3;
         if (prefix == 0 || name.size() == prefix || name.size() > prefix + 2)
            return -1;
         int step = 0;
         for (size_t i = prefix; i < name.size(); ++i)
         {
            if (name[i] < '0' || name[i] > '9')
               return -1;
            step = step * 10 + (name[i] - '0');
         }
         if (step >= kNoteSeqMaxSteps)
            return -1;
         kind = name[0];
         return step;
      }

      int NoteSequencerModule::stepCount() const
      {
         return std::max(1, std::min(kNoteSeqMaxSteps, static_cast<int>(std::lround(SliderPanelModule::getControlValue("steps")))));
      }

      int NoteSequencerModule::rowCount() const
      {
         return std::max(1, static_cast<int>(std::lround(SliderPanelModule::getControlValue("rows"))));
      }

      float NoteSequencerModule::gridTop() const
      {
         return slidersTop() - kHeaderHeight;
      }

      NoteSequencerModule::Hit NoteSequencerModule::hitAt(float worldX, float worldY) const
      {
         Hit hit;
         const float localX = worldX - mX - kSidePad;
         const float localY = worldY - mY - gridTop();
         if (localX < 0.0f || localX >= kCellWidth * static_cast<float>(stepCount()))
            return hit;
         hit.step = static_cast<int>(localX / kCellWidth);
         const float velocityTop = kGridHeight + kLaneGap;
         const float lengthTop = velocityTop + kVelocityLaneHeight + kLaneGap;
         if (localY >= 0.0f && localY < kGridHeight)
         {
            hit.lane = Lane::Grid;
            const int rows = rowCount();
            hit.row = rows - 1 - std::min(rows - 1, static_cast<int>(localY / (kGridHeight / static_cast<float>(rows))));
         }
         else if (localY >= velocityTop && localY < velocityTop + kVelocityLaneHeight)
         {
            hit.lane = Lane::Velocity;
            hit.value = 1.0f - (localY - velocityTop) / kVelocityLaneHeight;
         }
         else if (localY >= lengthTop && localY < lengthTop + kLengthLaneHeight)
         {
            hit.lane = Lane::Length;
            hit.value = (localX - static_cast<float>(hit.step) * kCellWidth) / kCellWidth;
         }
         hit.value = std::max(0.0f, std::min(1.0f, hit.value));
         return hit;
      }

      void NoteSequencerModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         SliderPanelModule::render(renderer, offsetX, offsetY, scale);

         const int steps = stepCount();
         const int rows = rowCount();
         const float left = (mX + offsetX + kSidePad) * scale;
         const float top = (mY + offsetY + gridTop()) * scale;
         const float cellW = kCellWidth * scale;
         const float cellH = kGridHeight / static_cast<float>(rows) * scale;
         const float gridW = cellW * static_cast<float>(kNoteSeqMaxSteps);

         renderer.fillColor(Color(0.08f, 0.08f, 0.1f, 1.0f));
         renderer.rect(left, top, gridW, kGridHeight * scale);
         renderer.fill();
         // Row 0 (the root) and every scale octave get a lighter band, as on the desktop grid.
         for (int row = 0; row < rows; ++row)
         {
            const float y = top + static_cast<float>(rows - 1 - row) * cellH;
            const float shade = row % 7 == 0 ? 0.16f : (row % 2 == 0 ? 0.11f : 0.09f);
            renderer.fillColor(Color(shade, shade, shade + 0.02f, 1.0f));
            renderer.rect(left, y + 0.5f * scale, cellW * static_cast<float>(steps), cellH - 1.0f * scale);
            renderer.fill();
         }
         for (int step = 0; step < steps; ++step)
         {
            const float x = left + static_cast<float>(step) * cellW;
            if (step % 4 == 0)
            {
               renderer.fillColor(Color(0.25f, 0.25f, 0.28f, 1.0f));
               renderer.rect(x, top, 1.0f * scale, kGridHeight * scale);
               renderer.fill();
            }
            const float velocity = mVelocity[step];
            if (velocity > 0.0f)
            {
               const int row = std::max(0, std::min(rows - 1, static_cast<int>(std::lround(mTone[step]))));
               const float y = top + static_cast<float>(rows - 1 - row) * cellH;
               const float lengthW = std::max(0.2f, mLength[step]) * (cellW - 2.0f * scale);
               renderer.fillColor(Color(0.95f, 0.6f, 0.2f, 0.35f + 0.65f * velocity));
               renderer.rect(x + 1.0f * scale, y + 1.0f * scale, lengthW, cellH - 2.0f * scale);
               renderer.fill();
            }
         }
         // Steps past `steps` are kept but greyed out.
         if (steps < kNoteSeqMaxSteps)
         {
            renderer.fillColor(Color(0.0f, 0.0f, 0.0f, 0.45f));
            renderer.rect(left + cellW * static_cast<float>(steps), top, cellW * static_cast<float>(kNoteSeqMaxSteps - steps),
                          kGridHeight * scale);
            renderer.fill();
         }

         const float velocityTop = top + (kGridHeight + kLaneGap) * scale;
         const float lengthTop = velocityTop + (kVelocityLaneHeight + kLaneGap) * scale;
         for (int step = 0; step < kNoteSeqMaxSteps; ++step)
         {
            const float x = left + static_cast<float>(step) * cellW;
            const float alpha = step < steps ? 1.0f : 0.35f;
            renderer.fillColor(Color(0.14f, 0.14f, 0.16f, alpha));
            renderer.rect(x + 1.0f * scale, velocityTop, cellW - 2.0f * scale, kVelocityLaneHeight * scale);
            renderer.rect(x + 1.0f * scale, lengthTop, cellW - 2.0f * scale, kLengthLaneHeight * scale);
            renderer.fill();
            const float velocityH = mVelocity[step] * kVelocityLaneHeight * scale;
            renderer.fillColor(Color(0.45f, 0.82f, 0.7f, alpha));
            renderer.rect(x + 1.0f * scale, velocityTop + kVelocityLaneHeight * scale - velocityH, cellW - 2.0f * scale,
                          velocityH);
            renderer.fill();
            renderer.fillColor(Color(0.55f, 0.65f, 0.95f, alpha));
            renderer.rect(x + 1.0f * scale, lengthTop, std::max(0.0f, mLength[step]) * (cellW - 2.0f * scale),
                          kLengthLaneHeight * scale);
            renderer.fill();
         }
      }

      void NoteSequencerModule::setControlValue(const std::string& name, float value)
      {
         char kind = 0;
         const int step = stepControl(name, kind);
         if (step < 0)
         {
            SliderPanelModule::setControlValue(name, value);
            return;
         }
         if (kind == 't')
            mTone[step] = std::max(0.0f, std::min(15.0f, std::round(value)));
         else if (kind == 'v')
            mVelocity[step] = std::max(0.0f, std::min(1.0f, value));
         else
            mLength[step] = std::max(0.0f, std::min(1.0f, value));
      }

      float NoteSequencerModule::getControlValue(const std::string& name) const
      {
         char kind = 0;
         const int step = stepControl(name, kind);
         if (step < 0)
            return SliderPanelModule::getControlValue(name);
         return kind == 't' ? mTone[step] : (kind == 'v' ? mVelocity[step] : mLength[step]);
      }

      bool NoteSequencerModule::handleMouseDown(float worldX, float worldY)
      {
         const Hit hit = hitAt(worldX, worldY);
         if (hit.lane == Lane::None)
            return SliderPanelModule::handleMouseDown(worldX, worldY);
         mDragLane = hit.lane;
         switch (hit.lane)
         {
            case Lane::Grid:
               if (mVelocity[hit.step] > 0.0f && static_cast<int>(std::lround(mTone[hit.step])) == hit.row)
               {
                  mVelocity[hit.step] = 0.0f;
               }
               else
               {
                  mTone[hit.step] = static_cast<float>(hit.row);
                  if (mVelocity[hit.step] <= 0.0f)
                     mVelocity[hit.step] = kDefaultVelocity;
               }
               break;
            case Lane::Velocity:
               mVelocity[hit.step] = hit.value;
               break;
            case Lane::Length:
               mLength[hit.step] = hit.value;
               break;
            case Lane::None:
               break;
         }
         return true;
      }

      bool NoteSequencerModule::handleMouseDrag(float worldX, float worldY, float dx, float dy)
      {
         if (mDragLane == Lane::None)
            return SliderPanelModule::handleMouseDrag(worldX, worldY, dx, dy);
         const Hit hit = hitAt(worldX, worldY);
         if (hit.step < 0)
            return true;
         // Painting across the grid sets tones; the lanes follow the pointer height.
         if (mDragLane == Lane::Grid && hit.lane == Lane::Grid)
         {
            mTone[hit.step] = static_cast<float>(hit.row);
            if (mVelocity[hit.step] <= 0.0f)
               mVelocity[hit.step] = kDefaultVelocity;
         }
         else if (mDragLane == Lane::Velocity)
         {
            const float localY = worldY - mY - gridTop() - kGridHeight - kLaneGap;
            mVelocity[hit.step] = std::max(0.0f, std::min(1.0f, 1.0f - localY / kVelocityLaneHeight));
         }
         else if (mDragLane == Lane::Length)
         {
            const float localX = worldX - mX - kSidePad - static_cast<float>(hit.step) * kCellWidth;
            mLength[hit.step] = std::max(0.0f, std::min(1.0f, localX / kCellWidth));
         }
         return true;
      }

      void NoteSequencerModule::handleMouseUp()
      {
         mDragLane = Lane::None;
         SliderPanelModule::handleMouseUp();
      }

      std::string NoteSequencerModule::controlNameAt(float worldX, float worldY) const
      {
         const Hit hit = hitAt(worldX, worldY);
         switch (hit.lane)
         {
            case Lane::Grid:
               return "tone" + std::to_string(hit.step);
            case Lane::Velocity:
               return "vel" + std::to_string(hit.step);
            case Lane::Length:
               return "len" + std::to_string(hit.step);
            case Lane::None:
               break;
         }
         return SliderPanelModule::controlNameAt(worldX, worldY);
      }

      bool NoteSequencerModule::controlRange(const std::string& name, float& minValue, float& maxValue) const
      {
         char kind = 0;
         if (stepControl(name, kind) < 0)
            return SliderPanelModule::controlRange(name, minValue, maxValue);
         minValue = 0.0f;
         maxValue = kind == 't' ? 15.0f : 1.0f;
         return true;
      }

   } // namespace wasm
} // namespace bespoke
