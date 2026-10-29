/**
 * BespokeSynth WASM - Canvas spawn menu (category chips, search, keyboard selection)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/ModuleHelp.h"
#include "BespokeWasm/PixelFont.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr float kMenuWidth = 280.0f;
         constexpr float kSearchHeight = 29.0f;
         constexpr float kCategoryHeight = 25.0f;
         constexpr float kRowHeight = 24.0f;
         constexpr int kMaxRows = 8;
         // Two lines of the selected module's help under the rows.
         constexpr float kDescriptionFont = 9.5f;
         constexpr float kDescriptionHeight = 34.0f;

         float spawnMenuHeight(int rows)
         {
            return kSearchHeight + kCategoryHeight + 8.0f + static_cast<float>(rows) * kRowHeight + 10.0f +
                   kDescriptionHeight;
         }
      }

      std::string ModuleCanvas::spawnMenuDescription() const
      {
         Lock lock(mMutex);
         if (!mSpawnMenuOpen)
            return {};
         const auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
         if (types.empty())
            return {};
         const int selected = std::min(mSpawnMenuSelectedIndex, static_cast<int>(types.size()) - 1);
         return ModuleHelp::instance().description(types[selected].type);
      }

      void ModuleCanvas::openSpawnMenu(float x, float y)
      {
         Lock lock(mMutex);
         mSpawnMenuOpen = true;
         mSpawnMenuX = x;
         mSpawnMenuY = y;
         mSpawnMenuSearch.clear();
         mSpawnMenuSelectedIndex = 0;
      }

      void ModuleCanvas::closeSpawnMenu()
      {
         Lock lock(mMutex);
         mSpawnMenuOpen = false;
         mSpawnMenuCategory = -1;
      }

      void ModuleCanvas::renderSpawnMenu(Renderer2D& renderer, int viewWidth, int viewHeight)
      {
         // Keep the palette native so it shares the canvas transform and Bespoke visual language.
         // TypeScript only forwards browser-only input; a DOM overlay would be easier to iterate
         // on, but would need duplicate placement, focus, and renderer-backend coordination.
         if (!mSpawnMenuOpen)
            return;

         const auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
         const int rows = std::min(static_cast<int>(types.size()), kMaxRows);
         const float menuHeight = spawnMenuHeight(rows);
         const float menuX = std::max(6.0f, std::min(mSpawnMenuX, static_cast<float>(viewWidth) - kMenuWidth - 6.0f));
         const float menuY = std::max(kTitleBarHeight + 4.0f,
                                      std::min(mSpawnMenuY, static_cast<float>(viewHeight) - menuHeight - 6.0f));
         mSpawnMenuRenderX = menuX;
         mSpawnMenuRenderY = menuY;

         renderer.fillColor(Color(0.12f, 0.12f, 0.14f, 0.98f));
         renderer.roundedRect(menuX, menuY, kMenuWidth, menuHeight, 5.0f);
         renderer.fill();
         renderer.strokeColor(Color(0.38f, 0.38f, 0.44f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(menuX, menuY, kMenuWidth, menuHeight, 5.0f);
         renderer.stroke();

         const std::string searchLabel = mSpawnMenuSearch.empty() ? "Search modules..." : mSpawnMenuSearch;
         drawText(renderer, menuX + 10.0f, textBaselineFromTop(renderer, menuY + 7.0f),
                  searchLabel.c_str(), mSpawnMenuSearch.empty() ? UITheme::kTextSecondary : UITheme::kTextPrimary, 12.0f);
         renderer.strokeColor(Color(0.28f, 0.28f, 0.33f, 1.0f));
         renderer.line(menuX + 8.0f, menuY + kSearchHeight, menuX + kMenuWidth - 8.0f, menuY + kSearchHeight);

         for (int i = 0; i < kSpawnMenuChipCount; ++i)
         {
            const SpawnMenuChip& chip = spawnMenuChip(i);
            const float chipX = menuX + 6.0f + static_cast<float>(i) * kSpawnMenuChipPitch;
            if (mSpawnMenuCategory == chip.category)
            {
               renderer.fillColor(Color(0.25f, 0.35f, 0.30f, 1.0f));
               renderer.roundedRect(chipX, menuY + kSearchHeight + 3.0f, kSpawnMenuChipWidth, 18.0f, 3.0f);
               renderer.fill();
            }
            drawText(renderer, chipX + 4.0f, textBaselineFromTop(renderer, menuY + kSearchHeight + 6.0f),
                     chip.label, UITheme::kTextSecondary, 9.0f);
         }

         const float rowsY = menuY + kSearchHeight + kCategoryHeight + 2.0f;
         if (types.empty())
         {
            drawText(renderer, menuX + 10.0f, textBaselineFromTop(renderer, rowsY + 5.0f),
                     "No matching modules", UITheme::kTextSecondary, 11.0f);
            return;
         }
         for (int i = 0; i < rows; ++i)
         {
            const float rowY = rowsY + static_cast<float>(i) * kRowHeight;
            if (i == mSpawnMenuSelectedIndex)
            {
               renderer.fillColor(Color(0.24f, 0.30f, 0.34f, 1.0f));
               renderer.rect(menuX + 5.0f, rowY, kMenuWidth - 10.0f, kRowHeight);
               renderer.fill();
            }
            drawText(renderer, menuX + 11.0f, textBaselineFromTop(renderer, rowY + 4.0f),
                     types[i].displayName.c_str(), UITheme::kTextPrimary, 12.0f);
         }

         const int selected = std::min(mSpawnMenuSelectedIndex, static_cast<int>(types.size()) - 1);
         const std::string description = ModuleHelp::instance().description(types[selected].type);
         if (description.empty())
            return;
         const float descriptionY = rowsY + static_cast<float>(rows) * kRowHeight + 6.0f;
         renderer.strokeColor(Color(0.28f, 0.28f, 0.33f, 1.0f));
         renderer.line(menuX + 8.0f, descriptionY, menuX + kMenuWidth - 8.0f, descriptionY);
         renderer.fontSize(kDescriptionFont);
         const auto lines = wrapText(renderer, description, kMenuWidth - 20.0f, 2);
         for (size_t i = 0; i < lines.size(); ++i)
         {
            drawText(renderer, menuX + 10.0f,
                     textBaselineForLine(descriptionY + 5.0f, static_cast<int>(i), kDescriptionFont, 1.0f),
                     lines[i].c_str(), UITheme::kTextSecondary, kDescriptionFont);
         }
      }

      void ModuleCanvas::handleSpawnMenuHoverLocked(float x, float y)
      {
         const auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
         const int rows = std::min(static_cast<int>(types.size()), kMaxRows);
         const float rowsY = mSpawnMenuRenderY + kSearchHeight + kCategoryHeight + 2.0f;
         if (x < mSpawnMenuRenderX || x > mSpawnMenuRenderX + kMenuWidth || y < rowsY)
            return;
         const int row = static_cast<int>((y - rowsY) / kRowHeight);
         if (row < rows)
            mSpawnMenuSelectedIndex = row;
      }

      void ModuleCanvas::handleSpawnMenuClickLocked(float x, float y)
      {
         const auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
         const int rows = std::min(static_cast<int>(types.size()), kMaxRows);
         const float menuHeight = spawnMenuHeight(rows);
         const float menuX = mSpawnMenuRenderX;
         const float menuY = mSpawnMenuRenderY;

         if (x >= menuX && x <= menuX + kMenuWidth && y >= menuY && y <= menuY + menuHeight)
         {
            const float categoryY = menuY + kSearchHeight + 3.0f;
            if (y >= categoryY && y <= categoryY + 18.0f)
            {
               const int chip = static_cast<int>(std::floor((x - menuX - 6.0f) / kSpawnMenuChipPitch));
               if (chip >= 0 && chip < kSpawnMenuChipCount)
               {
                  mSpawnMenuCategory = spawnMenuChip(chip).category;
                  mSpawnMenuSelectedIndex = 0;
               }
               return;
            }
            const float rowsY = menuY + kSearchHeight + kCategoryHeight + 2.0f;
            const int row = static_cast<int>((y - rowsY) / kRowHeight);
            if (y >= rowsY && row < rows)
            {
               const float worldX = screenToWorldX(mSpawnMenuX);
               const float worldY = screenToWorldY(mSpawnMenuY - kTitleBarHeight);
               createModule(types[row].type, worldX, worldY);
               closeSpawnMenu();
               return;
            }
            return;
         }
         closeSpawnMenu();
         return;
      }

      void ModuleCanvas::handleSpawnMenuKeyLocked(int keyCode, int modifiers)
      {
         static const int kKeyBackspace = 8;
         static const int kKeyEscape = 27;
         static const int kKeyEnter = 13;
         static const int kKeyUp = 38;
         static const int kKeyDown = 40;

         auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
         if (keyCode == kKeyEscape)
         {
            closeSpawnMenu();
            return;
         }
         if (keyCode == kKeyBackspace)
         {
            if (!mSpawnMenuSearch.empty())
               mSpawnMenuSearch.pop_back();
            mSpawnMenuSelectedIndex = 0;
            return;
         }
         if (keyCode == kKeyUp && !types.empty())
         {
            mSpawnMenuSelectedIndex = (mSpawnMenuSelectedIndex + static_cast<int>(types.size()) - 1) % static_cast<int>(types.size());
            return;
         }
         if (keyCode == kKeyDown && !types.empty())
         {
            mSpawnMenuSelectedIndex = (mSpawnMenuSelectedIndex + 1) % static_cast<int>(types.size());
            return;
         }
         if (keyCode == kKeyEnter && !types.empty())
         {
            const int selected = std::min(mSpawnMenuSelectedIndex, static_cast<int>(types.size()) - 1);
            createModule(types[selected].type, screenToWorldX(mSpawnMenuX),
                         screenToWorldY(mSpawnMenuY - kTitleBarHeight));
            closeSpawnMenu();
            return;
         }
         if ((modifiers & (2 | 4 | 8)) == 0 && keyCode >= 32 && keyCode <= 126)
         {
            const char character = static_cast<char>(std::tolower(static_cast<unsigned char>(keyCode)));
            if (std::isalnum(static_cast<unsigned char>(character)) || character == ' ' || character == '-' || character == '_')
            {
               mSpawnMenuSearch.push_back(character);
               mSpawnMenuSelectedIndex = 0;
            }
         }
         return;
      }

   } // namespace wasm
} // namespace bespoke
