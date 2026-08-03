/**
 * BespokeSynth WASM - Noise canvas module UI
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      NoiseModule::NoiseModule(int id)
      : Module(id, "noise", "Noise", ModuleCategory::Synth)
      {
         setSize(120, 80);
         addOutput("Out", PortType::Audio);
      }

      void NoiseModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         const float contentTop = screenY + (kTitleBarHeight + 14) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float textX = screenX + 10 * scale;

         drawText(renderer, textX, textBaselineForLine(contentTop, 0, valueFont, scale),
                  (mColor % 2) == 0 ? "White" : "Pink", UITheme::kTextPrimary, valueFont);

         char line[32];
         snprintf(line, sizeof(line), "Vol %.0f%%", mVolume * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, valueFont, scale),
                  line, UITheme::kTextValue, valueFont);
      }

      void NoiseModule::setControlValue(const std::string& name, float value)
      {
         if (name == "volume")
            mVolume = value;
         else if (name == "color")
            mColor = static_cast<int>(value);
      }

      float NoiseModule::getControlValue(const std::string& name) const
      {
         if (name == "volume")
            return mVolume;
         if (name == "color")
            return static_cast<float>(mColor);
         return 0.0f;
      }

   }
}
