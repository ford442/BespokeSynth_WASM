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
      OutputModule::OutputModule(int id)
      : Module(id, "output", "Output", ModuleCategory::Other)
      {
         setSize(120, 90);
      }

      void OutputModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         // Draw VU meter
         float meterX = screenX + 20 * scale;
         float meterY = screenY + (kTitleBarHeight + 15) * scale;
         renderer.drawVUMeter(meterX, meterY, 15 * scale, 45 * scale,
                              mLevel,
                              Color(0.2f, 0.8f, 0.3f, 1.0f),
                              Color(1.0f, 0.2f, 0.1f, 1.0f));

         renderer.drawVUMeter(meterX + 25 * scale, meterY, 15 * scale, 45 * scale,
                              mLevel * 0.9f,
                              Color(0.2f, 0.8f, 0.3f, 1.0f),
                              Color(1.0f, 0.2f, 0.1f, 1.0f));

         const float meterLabelFont = 9.0f * scale;
         const float meterLabelY = textBaselineFromTop(renderer, meterY + 48 * scale);
         drawText(renderer, meterX, meterLabelY, "L", UITheme::kTextSecondary, meterLabelFont);
         drawText(renderer, meterX + 25 * scale, meterLabelY, "R",
                  UITheme::kTextSecondary, meterLabelFont);
      }

      void OutputModule::setControlValue(const std::string& name, float value)
      {
         if (name == "level")
            mLevel = std::max(0.0f, std::min(1.0f, value));
      }

      float OutputModule::getControlValue(const std::string& name) const
      {
         if (name == "level")
            return mLevel;
         return 0.0f;
      }

   }
}
