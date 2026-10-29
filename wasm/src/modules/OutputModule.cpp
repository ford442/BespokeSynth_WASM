/**
 * BespokeSynth WASM
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/WidgetPainter.h"
#include <algorithm>
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

      namespace
      {
         // Local layout (world units from the module origin).
         constexpr float kToggleX = 72.0f;
         constexpr float kToggleY = 36.0f;
         constexpr float kToggleW = 40.0f;
         constexpr float kToggleH = 14.0f;
         constexpr float kMeterDecay = 0.85f; // per UI frame
         constexpr int kClipHold = 60; // UI frames (~1 s)
      }

      void OutputModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         // VU: true per-channel peaks before the limiter (values > 1 pin the meter and light CLIP).
         float meterX = screenX + 20 * scale;
         float meterY = screenY + (kTitleBarHeight + 15) * scale;
         for (int ch = 0; ch < 2; ++ch)
         {
            WidgetPainter(renderer).drawVUMeter(meterX + 25 * scale * static_cast<float>(ch), meterY, 15 * scale, 45 * scale,
                                                std::min(1.0f, mMeter[ch]),
                                                Color(0.2f, 0.8f, 0.3f, 1.0f),
                                                Color(1.0f, 0.2f, 0.1f, 1.0f));
         }

         const float meterLabelFont = 9.0f * scale;
         const float meterLabelY = textBaselineFromTop(renderer, meterY + 48 * scale);
         drawText(renderer, meterX, meterLabelY, "L", UITheme::kTextSecondary, meterLabelFont);
         drawText(renderer, meterX + 25 * scale, meterLabelY, "R",
                  UITheme::kTextSecondary, meterLabelFont);

         const float toggleX = screenX + kToggleX * scale;
         const float toggleY = screenY + kToggleY * scale;
         renderer.fillColor(mLimit ? UITheme::kAccentCyan : UITheme::kBgTrack);
         renderer.roundedRect(toggleX, toggleY, kToggleW * scale, kToggleH * scale, 3.0f * scale);
         renderer.fill();
         drawText(renderer, toggleX + 5 * scale, textBaselineCentered(toggleY + kToggleH * 0.5f * scale, meterLabelFont),
                  "LIMIT", mLimit ? UITheme::kBgDark : UITheme::kTextSecondary, meterLabelFont);

         const float ledY = toggleY + (kToggleH + 14.0f) * scale;
         renderer.fillColor(mClipHoldFrames > 0 ? Color(1.0f, 0.2f, 0.1f, 1.0f) : UITheme::kBgTrack);
         renderer.circle(toggleX + 6 * scale, ledY, 4.0f * scale);
         renderer.fill();
         drawText(renderer, toggleX + 14 * scale, textBaselineCentered(ledY, meterLabelFont), "CLIP",
                  UITheme::kTextSecondary, meterLabelFont);
         if (mClipHoldFrames > 0)
            --mClipHoldFrames;
      }

      bool OutputModule::hitLimitToggle(float worldX, float worldY) const
      {
         const float localX = worldX - mX;
         const float localY = worldY - mY;
         return localX >= kToggleX && localX <= kToggleX + kToggleW && localY >= kToggleY &&
                localY <= kToggleY + kToggleH;
      }

      bool OutputModule::handleMouseDown(float worldX, float worldY)
      {
         if (!hitLimitToggle(worldX, worldY))
            return false;
         mLimit = !mLimit;
         return true;
      }

      std::string OutputModule::controlNameAt(float worldX, float worldY) const
      {
         return hitLimitToggle(worldX, worldY) ? "limit" : std::string();
      }

      void OutputModule::setControlValue(const std::string& name, float value)
      {
         if (name == "level")
            mLevel = std::max(0.0f, std::min(1.0f, value));
         else if (name == "limit")
            mLimit = value >= 0.5f;
         else if (name == "meterL" || name == "meterR")
         {
            const int ch = name == "meterL" ? 0 : 1;
            mMeter[ch] = std::max(value, mMeter[ch] * kMeterDecay);
            if (value > 1.0f)
               mClipHoldFrames = kClipHold;
         }
      }

      float OutputModule::getControlValue(const std::string& name) const
      {
         if (name == "level")
            return mLevel;
         if (name == "limit")
            return mLimit ? 1.0f : 0.0f;
         if (name == "meterL")
            return mMeter[0];
         if (name == "meterR")
            return mMeter[1];
         return 0.0f;
      }

   }
}
