/**
 * BespokeSynth WASM - Delay canvas module UI
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      DelayModule::DelayModule(int id)
      : Module(id, "delay", "Delay", ModuleCategory::AudioEffect)
      {
         setSize(140, 100);
         addInput("In", PortType::Audio);
         addOutput("Out", PortType::Audio);
      }

      void DelayModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         const float contentTop = screenY + (kTitleBarHeight + 14) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float textX = screenX + 10 * scale;

         char line[48];
         snprintf(line, sizeof(line), "Time %.0f ms", mTime * 1000.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, valueFont, scale),
                  line, UITheme::kTextValue, valueFont);
         snprintf(line, sizeof(line), "Feedback %.0f%%", mFeedback * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, valueFont, scale),
                  line, UITheme::kTextSecondary, valueFont);
         snprintf(line, sizeof(line), "Mix %.0f%%", mMix * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 2, valueFont, scale),
                  line, UITheme::kTextValue, valueFont);
      }

      void DelayModule::setControlValue(const std::string& name, float value)
      {
         if (name == "time")
            mTime = value;
         else if (name == "feedback")
            mFeedback = value;
         else if (name == "mix")
            mMix = value;
      }

      float DelayModule::getControlValue(const std::string& name) const
      {
         if (name == "time")
            return mTime;
         if (name == "feedback")
            return mFeedback;
         if (name == "mix")
            return mMix;
         return 0.0f;
      }

   }
}
