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
      OscillatorModule::OscillatorModule(int id)
      : Module(id, "oscillator", "Oscillator", ModuleCategory::Synth)
      {
         setSize(160, 120);
         addInput("Pitch", PortType::Note);
         addInput("Mod", PortType::Modulation);
         addOutput("Out", PortType::Audio);
      }

      void OscillatorModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float labelFont = UITheme::kLabelFontSize * scale;
         const float lineFont = std::max(valueFont, labelFont);
         const float lineH = textLineSpacing(lineFont, scale);

         const float contentTop = screenY + (kTitleBarHeight + 22) * scale;
         const float textX = screenX + 10 * scale;

         char freqText[32];
         formatFrequency(mFrequency, freqText, sizeof(freqText));
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, lineFont, scale),
                  freqText, UITheme::kTextValue, valueFont);

         const char* waveNames[] = { "Sine", "Saw", "Square", "Triangle" };
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, lineFont, scale),
                  waveNames[mWaveform % 4], UITheme::kTextPrimary, labelFont);

         char volText[32];
         snprintf(volText, sizeof(volText), "Level %.0f%%", mVolume * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 2, lineFont, scale),
                  volText, UITheme::kTextSecondary, valueFont);

         // Mini waveform preview (matches selected shape)
         float wfX = screenX + 10 * scale;
         float wfY = contentTop + lineH * 3.0f;
         float wfW = 70 * scale;
         float wfH = 22 * scale;

         renderer.strokeColor(Color(0.3f, 0.8f, 0.5f, 0.85f));
         renderer.strokeWidth(1.5f * scale);
         for (int i = 0; i < 24; i++)
         {
            float t1 = (float)i / 24.0f;
            float t2 = (float)(i + 1) / 24.0f;
            float y1 = waveformSample(mWaveform, t1) * 0.45f;
            float y2 = waveformSample(mWaveform, t2) * 0.45f;
            renderer.line(wfX + t1 * wfW, wfY + wfH * 0.5f - y1 * wfH,
                          wfX + t2 * wfW, wfY + wfH * 0.5f - y2 * wfH);
         }
      }

      void OscillatorModule::setControlValue(const std::string& name, float value)
      {
         if (name == "frequency")
            mFrequency = value;
         else if (name == "volume")
            mVolume = value;
         else if (name == "waveform")
            mWaveform = static_cast<int>(value);
      }

      float OscillatorModule::getControlValue(const std::string& name) const
      {
         if (name == "frequency")
            return mFrequency;
         if (name == "volume")
            return mVolume;
         if (name == "waveform")
            return static_cast<float>(mWaveform);
         return 0.0f;
      }

      bool OscillatorModule::handleMouseDown(float worldX, float worldY)
      {
         const float localX = worldX - mX;
         const float localY = worldY - mY;

         // Click waveform name row to cycle shape
         if (localX >= 10.0f && localX <= mWidth - 10.0f &&
             localY >= kTitleBarHeight + 34.0f && localY <= kTitleBarHeight + 50.0f)
         {
            mWaveform = (mWaveform + 1) % 4;
            return true;
         }

         // Drag frequency in the upper content area
         if (localX >= 10.0f && localX <= mWidth - 10.0f &&
             localY >= kTitleBarHeight + 18.0f && localY <= kTitleBarHeight + 70.0f)
         {
            mDraggingFreq = true;
            return true;
         }
         return false;
      }

      bool OscillatorModule::handleMouseDrag(float worldX, float worldY, float dx, float dy)
      {
         (void)worldX;
         (void)worldY;
         if (!mDraggingFreq)
            return false;

         const float mult = powf(2.0f, -dy * 0.04f);
         mFrequency = std::max(20.0f, std::min(20000.0f, mFrequency * mult));
         return true;
      }

      void OscillatorModule::handleMouseUp()
      {
         mDraggingFreq = false;
      }

   }
}
