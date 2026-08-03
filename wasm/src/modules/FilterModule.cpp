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
      FilterModule::FilterModule(int id)
      : Module(id, "filter", "Filter", ModuleCategory::AudioEffect)
      {
         setSize(150, 110);
         addInput("In", PortType::Audio);
         addInput("CV", PortType::Modulation);
         addOutput("Out", PortType::Audio);
      }

      void FilterModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         const float contentTop = screenY + (kTitleBarHeight + 18) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float labelFont = UITheme::kLabelFontSize * scale;
         const float lineFont = std::max(valueFont, labelFont);
         const float lineH = textLineSpacing(lineFont, scale);
         const float textX = screenX + 10 * scale;

         const char* typeNames[] = { "Low-pass", "High-pass", "Band-pass" };
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, lineFont, scale),
                  typeNames[mType % 3], UITheme::kTextPrimary, labelFont);

         char cutText[48];
         snprintf(cutText, sizeof(cutText), "Cutoff ");
         formatFrequency(mCutoff, cutText + 7, sizeof(cutText) - 7);
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, lineFont, scale),
                  cutText, UITheme::kTextValue, valueFont);

         char resText[32];
         snprintf(resText, sizeof(resText), "Resonance %.2f", mResonance);
         drawText(renderer, textX, textBaselineForLine(contentTop, 2, lineFont, scale),
                  resText, UITheme::kTextSecondary, valueFont);

         // Simple filter response curve
         float curveX = screenX + 10 * scale;
         float curveY = contentTop + lineH * 3.0f;
         float curveW = 80 * scale;
         float curveH = 20 * scale;

         renderer.strokeColor(Color(0.8f, 0.5f, 0.2f, 0.8f));
         renderer.strokeWidth(1.5f * scale);
         for (int i = 0; i < 15; i++)
         {
            float t1 = (float)i / 15.0f;
            float t2 = (float)(i + 1) / 15.0f;
            float cutNorm = mCutoff / 20000.0f;
            float y1 = (t1 < cutNorm) ? 1.0f : expf(-(t1 - cutNorm) * 10.0f);
            float y2 = (t2 < cutNorm) ? 1.0f : expf(-(t2 - cutNorm) * 10.0f);
            renderer.line(curveX + t1 * curveW, curveY + curveH - y1 * curveH,
                          curveX + t2 * curveW, curveY + curveH - y2 * curveH);
         }
      }

      void FilterModule::setControlValue(const std::string& name, float value)
      {
         if (name == "cutoff")
            mCutoff = value;
         else if (name == "resonance")
            mResonance = value;
         else if (name == "type")
            mType = static_cast<int>(value);
      }

      float FilterModule::getControlValue(const std::string& name) const
      {
         if (name == "cutoff")
            return mCutoff;
         if (name == "resonance")
            return mResonance;
         if (name == "type")
            return static_cast<float>(mType);
         return 0.0f;
      }

   }
}
