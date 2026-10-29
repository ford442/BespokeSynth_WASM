/**
 * BespokeSynth WASM - Pulse To Note canvas module UI
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      PulseToNoteModule::PulseToNoteModule(int id)
      : Module(id, "pulsetonote", "Pulse To Note", ModuleCategory::NoteEffect)
      {
         setSize(150, 80);
      }

      void PulseToNoteModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         const float contentTop = screenY + (kTitleBarHeight + 14) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float textX = screenX + 10 * scale;

         char line[48];
         snprintf(line, sizeof(line), "Pitch %d", mPitch);
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, valueFont, scale),
                  line, UITheme::kTextValue, valueFont);
         snprintf(line, sizeof(line), "Gate %.0f%%", mGate * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, valueFont, scale),
                  line, UITheme::kTextSecondary, valueFont);
      }

      std::string PulseToNoteModule::controlNameAt(float worldX, float worldY) const
      {
         return textLineControlAt(worldX, worldY, kTitleBarHeight + 14.0f, UITheme::kValueFontSize,
                                  { "pitch", "gate" });
      }

      void PulseToNoteModule::setControlValue(const std::string& name, float value)
      {
         if (name == "pitch")
            mPitch = static_cast<int>(value);
         else if (name == "gate")
            mGate = value;
      }

      float PulseToNoteModule::getControlValue(const std::string& name) const
      {
         if (name == "pitch")
            return static_cast<float>(mPitch);
         if (name == "gate")
            return mGate;
         return 0.0f;
      }

   }
}
