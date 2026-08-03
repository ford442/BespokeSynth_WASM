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
      TransportModule::TransportModule(int id)
      : Module(id, "transport", "Transport", ModuleCategory::Other)
      {
         setSize(250, 60);
      }

      void TransportModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         float w = mWidth * scale;
         float h = mHeight * scale;

         // Transport background
         renderer.fillColor(Color(0.14f, 0.14f, 0.16f, 0.95f));
         renderer.roundedRect(screenX, screenY, w, h, 4.0f * scale);
         renderer.fill();

         renderer.strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(screenX, screenY, w, h, 4.0f * scale);
         renderer.stroke();

         // Play/Stop indicator
         float btnX = screenX + 10 * scale;
         float btnY = screenY + 15 * scale;

         if (mPlaying)
         {
            renderer.fillColor(Color(0.3f, 0.8f, 0.4f, 1.0f));
            // Draw pause bars
            renderer.rect(btnX, btnY, 4 * scale, 14 * scale);
            renderer.fill();
            renderer.rect(btnX + 7 * scale, btnY, 4 * scale, 14 * scale);
            renderer.fill();
         }
         else
         {
            renderer.fillColor(Color(0.6f, 0.6f, 0.65f, 1.0f));
            // Draw play triangle
            renderer.beginPath();
            renderer.moveTo(btnX, btnY);
            renderer.lineTo(btnX + 12 * scale, btnY + 7 * scale);
            renderer.lineTo(btnX, btnY + 14 * scale);
            renderer.closePath();
            renderer.fill();
         }

         // BPM display
         char bpmText[32];
         snprintf(bpmText, sizeof(bpmText), "%.1f BPM", mBPM);
         drawText(renderer, btnX + 25 * scale, textBaselineFromTop(renderer, screenY + 10 * scale),
                  bpmText, UITheme::kTextPrimary, 14.0f * scale);

         // Time signature
         char timeSigText[16];
         snprintf(timeSigText, sizeof(timeSigText), "%d/%d", mTimeSigTop, mTimeSigBottom);
         drawText(renderer, screenX + 140 * scale, textBaselineFromTop(renderer, screenY + 10 * scale),
                  timeSigText, UITheme::kTextSecondary, 11.0f * scale);

         // Swing
         char swingText[16];
         snprintf(swingText, sizeof(swingText), "Swing %.0f%%", mSwing * 100.0f);
         drawText(renderer, screenX + 185 * scale, textBaselineFromTop(renderer, screenY + 10 * scale),
                  swingText, UITheme::kTextSecondary, 11.0f * scale);

         const char* stateText = mPlaying ? "Playing" : "Stopped";
         drawText(renderer, screenX + 10 * scale, textBaselineFromTop(renderer, screenY + 38 * scale),
                  stateText, mPlaying ? UITheme::kAccentGreen : UITheme::kTextSecondary, 9.0f * scale);

         drawText(renderer, screenX + 70 * scale, textBaselineFromTop(renderer, screenY + 38 * scale),
                  "Space: play/stop", UITheme::kTextSecondary, 9.0f * scale);
      }

      void TransportModule::setControlValue(const std::string& name, float value)
      {
         if (name == "bpm")
            mBPM = value;
         else if (name == "swing")
            mSwing = value;
         else if (name == "playing")
            mPlaying = value > 0.5f;
         else if (name == "timesig_top")
            mTimeSigTop = static_cast<int>(value);
         else if (name == "timesig_bottom")
            mTimeSigBottom = static_cast<int>(value);
      }

      float TransportModule::getControlValue(const std::string& name) const
      {
         if (name == "bpm")
            return mBPM;
         if (name == "swing")
            return mSwing;
         if (name == "playing")
            return mPlaying ? 1.0f : 0.0f;
         return 0.0f;
      }

   }
}
