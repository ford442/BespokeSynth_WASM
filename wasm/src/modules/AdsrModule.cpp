/**
 * BespokeSynth WASM - ADSR canvas module UI
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      AdsrModule::AdsrModule(int id)
      : Module(id, "adsr", "ADSR", ModuleCategory::AudioEffect)
      {
         setSize(150, 110);
         addInput("In", PortType::Audio);
         addInput("Gate", PortType::Note);
         addOutput("Out", PortType::Audio);
      }

      void AdsrModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         const float contentTop = screenY + (kTitleBarHeight + 14) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float lineFont = valueFont;
         const float textX = screenX + 10 * scale;

         char line[48];
         snprintf(line, sizeof(line), "A %.0f ms", mAttack);
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, lineFont, scale),
                  line, UITheme::kTextValue, valueFont);
         snprintf(line, sizeof(line), "D %.0f ms", mDecay);
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, lineFont, scale),
                  line, UITheme::kTextSecondary, valueFont);
         snprintf(line, sizeof(line), "S %.0f%%", mSustain * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 2, lineFont, scale),
                  line, UITheme::kTextValue, valueFont);
         snprintf(line, sizeof(line), "R %.0f ms", mRelease);
         drawText(renderer, textX, textBaselineForLine(contentTop, 3, lineFont, scale),
                  line, UITheme::kTextSecondary, valueFont);
      }

      void AdsrModule::setControlValue(const std::string& name, float value)
      {
         if (name == "attack")
            mAttack = value;
         else if (name == "decay")
            mDecay = value;
         else if (name == "sustain")
            mSustain = value;
         else if (name == "release")
            mRelease = value;
      }

      float AdsrModule::getControlValue(const std::string& name) const
      {
         if (name == "attack")
            return mAttack;
         if (name == "decay")
            return mDecay;
         if (name == "sustain")
            return mSustain;
         if (name == "release")
            return mRelease;
         return 0.0f;
      }

   }
}
