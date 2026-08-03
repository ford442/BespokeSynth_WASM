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
      const char* const ScaleModule::kNoteNames[] = {
         "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
      };

      const char* const ScaleModule::kScaleNames[] = {
         "Major", "Minor", "Dorian", "Mixolydian", "Pentatonic", "Blues", "Chromatic"
      };

      ScaleModule::ScaleModule(int id)
      : Module(id, "scale", "Scale", ModuleCategory::Other)
      {
         setSize(180, 70);
      }

      void ScaleModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         float w = mWidth * scale;
         float h = mHeight * scale;

         // Background
         renderer.fillColor(Color(0.14f, 0.14f, 0.16f, 0.95f));
         renderer.roundedRect(screenX, screenY, w, h, 4.0f * scale);
         renderer.fill();

         renderer.strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(screenX, screenY, w, h, 4.0f * scale);
         renderer.stroke();

         // Root note + scale type
         drawText(renderer, screenX + 10 * scale, textBaselineFromTop(renderer, screenY + 6 * scale),
                  kNoteNames[mRootNote % 12], UITheme::kTextPrimary, 13.0f * scale);

         drawText(renderer, screenX + 38 * scale, textBaselineFromTop(renderer, screenY + 6 * scale),
                  kScaleNames[mScaleType % 7], UITheme::kTextSecondary, 11.0f * scale);

         // Piano keys visualization (mini)
         float keyX = screenX + 10 * scale;
         float keyY = screenY + 32 * scale;
         float keyW = 10 * scale;
         float keyH = 22 * scale;

         for (int i = 0; i < 12; i++)
         {
            bool isBlack = (i == 1 || i == 3 || i == 6 || i == 8 || i == 10);
            bool isRoot = (i == mRootNote);

            if (isRoot)
            {
               renderer.fillColor(Color(0.4f, 0.7f, 0.9f, 1.0f));
            }
            else if (isBlack)
            {
               renderer.fillColor(Color(0.15f, 0.15f, 0.17f, 1.0f));
            }
            else
            {
               renderer.fillColor(Color(0.85f, 0.85f, 0.87f, 1.0f));
            }

            renderer.rect(keyX + i * (keyW + 1), keyY, keyW, isBlack ? keyH * 0.6f : keyH);
            renderer.fill();
         }

         // Label below keys
         drawText(renderer, screenX + 10 * scale, textBaselineFromTop(renderer, keyY + keyH + 4 * scale),
                  "Root / scale", UITheme::kTextSecondary, 9.0f * scale);
      }

      void ScaleModule::setControlValue(const std::string& name, float value)
      {
         if (name == "root")
            mRootNote = static_cast<int>(value) % 12;
         else if (name == "type")
            mScaleType = static_cast<int>(value) % 7;
      }

      float ScaleModule::getControlValue(const std::string& name) const
      {
         if (name == "root")
            return static_cast<float>(mRootNote);
         if (name == "type")
            return static_cast<float>(mScaleType);
         return 0.0f;
      }

   }
}
