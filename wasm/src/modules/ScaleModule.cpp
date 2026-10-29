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
         fillEqualTemperament(mTuningTable);
      }

      void ScaleModule::setStringProperty(const std::string& name, const std::string& value)
      {
         if (name == "scl")
            mScl = value;
         else if (name == "kbm")
            mKbm = value;
         else if (name == "sclName")
            mSclName = value;
         else if (name == "kbmName")
            mKbmName = value;
         else
            return;
         if (name == "scl" || name == "kbm")
            rebuildTuning();
      }

      std::string ScaleModule::getStringProperty(const std::string& name) const
      {
         if (name == "scl")
            return mScl;
         if (name == "kbm")
            return mKbm;
         if (name == "sclName")
            return mSclName;
         if (name == "kbmName")
            return mKbmName;
         if (name == "tuningError")
            return mTuningError;
         if (name == "tuningDescription")
            return mTuningDescription;
         return {};
      }

      std::vector<std::string> ScaleModule::stringPropertyNames() const
      {
         // Only non-empty values serialize, so 12-TET patches stay unchanged.
         std::vector<std::string> names;
         if (!mScl.empty())
            names.insert(names.end(), { "scl", "sclName" });
         if (!mKbm.empty())
            names.insert(names.end(), { "kbm", "kbmName" });
         return names;
      }

      void ScaleModule::rebuildTuning()
      {
         mCustomTuning = false;
         mTuningDegrees = 12;
         mTuningDescription.clear();
         mTuningError.clear();
         if (mScl.empty() && mKbm.empty())
         {
            fillEqualTemperament(mTuningTable);
            return;
         }
         const TuningParseResult result = buildTuningTable(mScl, mKbm, mTuningTable);
         if (!result.ok)
         {
            mTuningError = result.error;
            printf("ScaleModule: tuning rejected: %s\n", result.error.c_str());
            return;
         }
         mCustomTuning = true;
         mTuningDegrees = result.scaleCount;
         mTuningDescription = result.description;
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

            renderer.rect(keyX + static_cast<float>(i) * (keyW + 1), keyY, keyW, isBlack ? keyH * 0.6f : keyH);
            renderer.fill();
         }

         // Label below keys: the Scala file when one is loaded, else the hint.
         char tuningLabel[48];
         if (mCustomTuning)
            snprintf(tuningLabel, sizeof(tuningLabel), "%.28s (%d)",
                     mSclName.empty() ? "Scala" : mSclName.c_str(), mTuningDegrees);
         else if (!mTuningError.empty())
            snprintf(tuningLabel, sizeof(tuningLabel), "Bad .scl - 12-TET");
         else
            snprintf(tuningLabel, sizeof(tuningLabel), "Root / scale - drop .scl");
         drawText(renderer, screenX + 10 * scale, textBaselineFromTop(renderer, keyY + keyH + 4 * scale),
                  tuningLabel, mCustomTuning ? UITheme::kTextValue : UITheme::kTextSecondary, 9.0f * scale);
      }

      std::string ScaleModule::controlNameAt(float worldX, float worldY) const
      {
         // Mirrors render(): root name then scale type on the top row, root keys below.
         const float localX = worldX - mX;
         const float localY = worldY - mY;
         if (localX < 0.0f || localX > mWidth || localY < 0.0f || localY > mHeight)
            return {};
         if (localY < 28.0f)
            return localX < 36.0f ? "root" : "type";
         return localY < 54.0f ? "root" : std::string();
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
