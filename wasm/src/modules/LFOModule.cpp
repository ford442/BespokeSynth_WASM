/**
 * BespokeSynth WASM
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <cmath>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      LFOModule::LFOModule(int id)
      : Module(id, "lfo", "LFO", ModuleCategory::Modulator)
      {
         setSize(130, 100);
         addOutput("Mod", PortType::Modulation);
      }

      void LFOModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         const float contentTop = screenY + (kTitleBarHeight + 14) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float labelFont = UITheme::kLabelFontSize * scale;
         const float lineFont = std::max(valueFont, labelFont);
         const float lineH = textLineSpacing(lineFont, scale);
         const float textX = screenX + 10 * scale;

         const char* shapeNames[] = { "Sine", "Triangle", "Saw", "Square" };

         char rateText[32];
         snprintf(rateText, sizeof(rateText), "Rate %.2f Hz", mRate);
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, lineFont, scale),
                  rateText, UITheme::kTextValue, valueFont);

         char depthText[32];
         snprintf(depthText, sizeof(depthText), "Depth %.0f%%", mDepth * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, lineFont, scale),
                  depthText, UITheme::kTextSecondary, valueFont);

         drawText(renderer, textX, textBaselineForLine(contentTop, 2, lineFont, scale),
                  shapeNames[mShape % 4], UITheme::kTextPrimary, labelFont);

         // Draw LFO waveform
         float wfX = screenX + 10 * scale;
         float wfY = contentTop + lineH * 3.0f;
         float wfW = 80 * scale;
         float wfH = 30 * scale;

         renderer.strokeColor(Color(0.7f, 0.7f, 0.2f, 0.9f));
         renderer.strokeWidth(1.5f * scale);
         for (int i = 0; i < 20; i++)
         {
            float t1 = (float)i / 20.0f;
            float t2 = (float)(i + 1) / 20.0f;
            float y1 = waveformSample(mShape, t1) * 0.5f * mDepth;
            float y2 = waveformSample(mShape, t2) * 0.5f * mDepth;
            renderer.line(wfX + t1 * wfW, wfY + wfH * 0.5f - y1 * wfH,
                          wfX + t2 * wfW, wfY + wfH * 0.5f - y2 * wfH);
         }
      }

      void LFOModule::setControlValue(const std::string& name, float value)
      {
         if (name == "rate")
            mRate = value;
         else if (name == "depth")
            mDepth = value;
         else if (name == "shape")
            mShape = static_cast<int>(value);
      }

      float LFOModule::getControlValue(const std::string& name) const
      {
         if (name == "rate")
            return mRate;
         if (name == "depth")
            return mDepth;
         if (name == "shape")
            return static_cast<float>(mShape);
         return 0.0f;
      }

   }
}
