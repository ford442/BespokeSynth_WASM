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
      GainModule::GainModule(int id)
      : Module(id, "gain", "Gain", ModuleCategory::AudioEffect)
      {
         setSize(120, 80);
         addInput("In", PortType::Audio);
         addInput("Mod", PortType::Modulation);
         addOutput("Out", PortType::Audio);
      }

      void GainModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         // Draw gain slider
         float sliderX = screenX + 10 * scale;
         float sliderY = screenY + (kTitleBarHeight + 20) * scale;
         float sliderW = 100 * scale;
         float sliderH = 16 * scale;

         renderer.drawSlider(sliderX, sliderY, sliderW, sliderH,
                             mGain,
                             Color(0.2f, 0.2f, 0.22f, 1.0f),
                             Color(0.8f, 0.5f, 0.2f, 1.0f));

         char gainText[16];
         formatGainDb(mGain, gainText, sizeof(gainText));
         drawText(renderer, sliderX, textBaselineFromTop(renderer, sliderY + sliderH + 4 * scale),
                  gainText, UITheme::kTextValue, UITheme::kValueFontSize * scale);
      }

      void GainModule::setControlValue(const std::string& name, float value)
      {
         if (name == "gain")
            mGain = value;
      }

      float GainModule::getControlValue(const std::string& name) const
      {
         if (name == "gain")
            return mGain;
         return 0.0f;
      }

      bool GainModule::handleMouseDown(float worldX, float worldY)
      {
         const float sliderX = mX + 10.0f;
         const float sliderY = mY + kTitleBarHeight + 20.0f;
         const float sliderW = 100.0f;
         const float sliderH = 16.0f;

         if (worldX >= sliderX && worldX <= sliderX + sliderW &&
             worldY >= sliderY && worldY <= sliderY + sliderH)
         {
            mDraggingSlider = true;
            mGain = std::max(0.0f, std::min(1.0f, (worldX - sliderX) / sliderW));
            return true;
         }
         return false;
      }

      bool GainModule::handleMouseDrag(float worldX, float worldY, float dx, float dy)
      {
         (void)worldY;
         (void)dx;
         (void)dy;
         if (!mDraggingSlider)
            return false;

         const float sliderX = mX + 10.0f;
         const float sliderW = 100.0f;
         mGain = std::max(0.0f, std::min(1.0f, (worldX - sliderX) / sliderW));
         return true;
      }

      void GainModule::handleMouseUp()
      {
         mDraggingSlider = false;
      }

   }
}
