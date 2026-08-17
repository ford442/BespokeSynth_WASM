/**
 * BespokeSynth WASM - Step sequencer canvas UI
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      StepSequencerModule::StepSequencerModule(int id)
      : Module(id, "stepsequencer", "Step Sequencer", ModuleCategory::Pulse)
      {
         setSize(220, 92);
      }

      bool StepSequencerModule::isStepActive(int step) const
      {
         if (step < 0 || step >= kStepCount)
            return false;
         return (mPatternMask & (1 << step)) != 0;
      }

      void StepSequencerModule::setStepActive(int step, bool active)
      {
         if (step < 0 || step >= kStepCount)
            return;
         if (active)
            mPatternMask |= (1 << step);
         else
            mPatternMask &= ~(1 << step);
      }

      void StepSequencerModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         const float screenX = (mX + offsetX) * scale;
         const float screenY = (mY + offsetY) * scale;
         const float contentTop = screenY + (kTitleBarHeight + 8) * scale;
         const float textX = screenX + 8 * scale;
         const float valueFont = UITheme::kValueFontSize * scale;

         char header[48];
         snprintf(header, sizeof(header), "Pitch %d  Gate %.0f%%", mPitch, mGate * 100.0f);
         drawText(renderer, textX, textBaselineFromTop(contentTop, valueFont),
                  header, UITheme::kTextSecondary, valueFont);

         const float gridTop = contentTop + textLineSpacing(valueFont, scale) + 2 * scale;
         const float cell = 11.0f * scale;
         const float gap = 2.0f * scale;
         for (int i = 0; i < mSteps; ++i)
         {
            const float cx = textX + (i % 8) * (cell + gap);
            const float cy = gridTop + (i / 8) * (cell + gap);
            const bool on = isStepActive(i);
            const bool beat = (i % 4) == 0;
            if (on)
               renderer.fillColor(Color(0.95f, 0.45f, 0.35f, 1.0f));
            else
               renderer.fillColor(beat ? Color(0.28f, 0.28f, 0.32f, 1.0f)
                                       : Color(0.18f, 0.18f, 0.20f, 1.0f));
            renderer.roundedRect(cx, cy, cell, cell, 2.0f * scale);
            renderer.fill();
         }
      }

      bool StepSequencerModule::handleMouseDown(float worldX, float worldY)
      {
         const float localX = worldX - mX;
         const float localY = worldY - mY;
         const float contentTop = kTitleBarHeight + 8.0f;
         const float valueFont = UITheme::kValueFontSize;
         const float gridTop = contentTop + textLineSpacing(valueFont, 1.0f) + 2.0f;
         const float cell = 11.0f;
         const float gap = 2.0f;
         const float textX = 8.0f;

         for (int i = 0; i < mSteps; ++i)
         {
            const float cx = textX + (i % 8) * (cell + gap);
            const float cy = gridTop + (i / 8) * (cell + gap);
            if (localX >= cx && localX <= cx + cell && localY >= cy && localY <= cy + cell)
            {
               setStepActive(i, !isStepActive(i));
               return true;
            }
         }
         return false;
      }

      void StepSequencerModule::setControlValue(const std::string& name, float value)
      {
         if (name == "pattern")
            mPatternMask = static_cast<int>(value) & 0xFFFF;
         else if (name == "pitch")
            mPitch = std::max(0, std::min(127, static_cast<int>(std::lround(value))));
         else if (name == "gate")
            mGate = std::max(0.05f, std::min(1.0f, value));
         else if (name == "steps")
            mSteps = std::max(1, std::min(kStepCount, static_cast<int>(std::lround(value))));
      }

      float StepSequencerModule::getControlValue(const std::string& name) const
      {
         if (name == "pattern")
            return static_cast<float>(mPatternMask);
         if (name == "pitch")
            return static_cast<float>(mPitch);
         if (name == "gate")
            return mGate;
         if (name == "steps")
            return static_cast<float>(mSteps);
         return 0.0f;
      }

   }
}
