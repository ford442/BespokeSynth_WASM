/**
 * BespokeSynth WASM - Looper canvas module
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      LooperModule::LooperModule(int id)
      : Module(id, "looper", "Looper", ModuleCategory::AudioEffect)
      {
         setSize(170, 96);
      }

      void LooperModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         const float screenX = (mX + offsetX) * scale;
         const float screenY = (mY + offsetY) * scale;
         const float btnY = screenY + (kTitleBarHeight + 12) * scale;
         const float btnH = 22 * scale;
         const float btnW = 48 * scale;

         const bool rec = mCommand == 1;
         const bool dub = mCommand == 2;
         const bool play = mCommand == 3;
         renderer.drawButton(screenX + 8 * scale, btnY, btnW, btnH, "Rec", rec, false);
         renderer.drawButton(screenX + 62 * scale, btnY, btnW, btnH, "Dub", dub, false);
         renderer.drawButton(screenX + 116 * scale, btnY, btnW, btnH, "Play", play, false);

         char line[48];
         snprintf(line, sizeof(line), "%d bar  mix %.0f%%", mBars, mMix * 100.0f);
         drawText(renderer, screenX + 8 * scale, btnY + btnH + 16 * scale, line,
                  UITheme::kTextValue, 10.0f * scale);
      }

      void LooperModule::setControlValue(const std::string& name, float value)
      {
         if (name == "mix")
            mMix = value;
         else if (name == "bars")
            mBars = std::max(1, std::min(8, static_cast<int>(value)));
         else if (name == "command")
            mCommand = static_cast<int>(value);
      }

      float LooperModule::getControlValue(const std::string& name) const
      {
         if (name == "mix")
            return mMix;
         if (name == "bars")
            return static_cast<float>(mBars);
         if (name == "command")
            return static_cast<float>(mCommand);
         return 0.0f;
      }

      bool LooperModule::handleMouseDown(float worldX, float worldY)
      {
         const float btnY = mY + kTitleBarHeight + 12.0f;
         const float btnH = 22.0f;
         const float btnW = 48.0f;
         if (worldY < btnY || worldY > btnY + btnH)
         {
            if (worldY > btnY + btnH && worldY < btnY + btnH + 20.0f)
            {
               mBars = mBars >= 8 ? 1 : mBars * 2;
               return true;
            }
            return false;
         }

         if (worldX >= mX + 8.0f && worldX <= mX + 8.0f + btnW)
         {
            mCommand = mCommand == 1 ? 4 : 1;
            return true;
         }
         if (worldX >= mX + 62.0f && worldX <= mX + 62.0f + btnW)
         {
            mCommand = mCommand == 2 ? 3 : 2;
            return true;
         }
         if (worldX >= mX + 116.0f && worldX <= mX + 116.0f + btnW)
         {
            mCommand = mCommand == 3 ? 4 : 3;
            return true;
         }
         return false;
      }

   } // namespace wasm
} // namespace bespoke
