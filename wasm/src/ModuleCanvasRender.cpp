/**
 * BespokeSynth WASM
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/ModuleFactory.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include "BespokeWasm/AudioAnalysis.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/PixelFont.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      Color ModuleCanvas::getCategoryColor(ModuleCategory cat)
      {
         switch (cat)
         {
            case ModuleCategory::Instrument:
               return Color(0.2f, 0.5f, 0.8f, 1.0f);
            case ModuleCategory::NoteEffect:
               return Color(0.6f, 0.3f, 0.7f, 1.0f);
            case ModuleCategory::Synth:
               return Color(0.3f, 0.7f, 0.5f, 1.0f);
            case ModuleCategory::AudioEffect:
               return Color(0.8f, 0.5f, 0.2f, 1.0f);
            case ModuleCategory::Modulator:
               return Color(0.7f, 0.7f, 0.2f, 1.0f);
            case ModuleCategory::Pulse:
               return Color(0.8f, 0.3f, 0.3f, 1.0f);
            default:
               return Color(0.4f, 0.4f, 0.45f, 1.0f);
         }
      }

      void ModuleCanvas::renderTitleBar(Renderer2D& renderer, int viewWidth)
      {
         float w = static_cast<float>(viewWidth);

         // Title bar background
         renderer.fillColor(Color(0.1f, 0.1f, 0.12f, 0.95f));
         renderer.rect(0, 0, w, kTitleBarHeight);
         renderer.fill();

         // Title bar bottom border
         renderer.strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.line(0, kTitleBarHeight, w, kTitleBarHeight);

         // Logo / title
         drawText(renderer, 10.0f, textBaselineFromTop(renderer, 8.0f),
                  "BespokeSynth", UITheme::kTextPrimary, 14.0f);

         float btnX = 140.0f;
         float btnW = 112.0f;
         float btnH = 24.0f;
         float btnY = 8.0f;
         renderer.fillColor(mSpawnMenuOpen ? Color(0.25f, 0.25f, 0.30f, 1.0f)
                                           : Color(0.18f, 0.18f, 0.20f, 1.0f));
         renderer.roundedRect(btnX, btnY, btnW, btnH, 3.0f);
         renderer.fill();
         renderer.strokeColor(Color(0.30f, 0.70f, 0.50f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(btnX, btnY, btnW, btnH, 3.0f);
         renderer.stroke();
         drawText(renderer, btnX + 8.0f, textBaselineFromTop(renderer, btnY + 4.0f),
                  "Add module  /", UITheme::kTextSecondary, 11.0f);
      }

      void ModuleCanvas::renderSpawnMenu(Renderer2D& renderer, int viewWidth, int viewHeight)
      {
         // Keep the palette native so it shares the canvas transform and Bespoke visual language.
         // TypeScript only forwards browser-only input; a DOM overlay would be easier to iterate
         // on, but would need duplicate placement, focus, and renderer-backend coordination.
         if (!mSpawnMenuOpen)
            return;

         const auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
         constexpr float kMenuWidth = 280.0f;
         constexpr float kSearchHeight = 29.0f;
         constexpr float kCategoryHeight = 25.0f;
         constexpr float kRowHeight = 24.0f;
         constexpr int kMaxRows = 8;
         const int rows = std::min(static_cast<int>(types.size()), kMaxRows);
         const float menuHeight = kSearchHeight + kCategoryHeight + 8.0f + rows * kRowHeight + 10.0f;
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

         const char* categories[] = { "All", "Synth", "FX", "Mod", "Pulse", "Other" };
         const int categoryValues[] = {
            -1,
            static_cast<int>(ModuleCategory::Synth),
            static_cast<int>(ModuleCategory::AudioEffect),
            static_cast<int>(ModuleCategory::Modulator),
            static_cast<int>(ModuleCategory::Pulse),
            static_cast<int>(ModuleCategory::Other)
         };
         constexpr int kCategoryCount = 6;
         for (int i = 0; i < kCategoryCount; ++i)
         {
            const float chipX = menuX + 6.0f + i * 44.5f;
            if (mSpawnMenuCategory == categoryValues[i])
            {
               renderer.fillColor(Color(0.25f, 0.35f, 0.30f, 1.0f));
               renderer.roundedRect(chipX, menuY + kSearchHeight + 3.0f, 42.0f, 18.0f, 3.0f);
               renderer.fill();
            }
            drawText(renderer, chipX + 4.0f, textBaselineFromTop(renderer, menuY + kSearchHeight + 6.0f),
                     categories[i], UITheme::kTextSecondary, 9.0f);
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
            const float rowY = rowsY + i * kRowHeight;
            if (i == mSpawnMenuSelectedIndex)
            {
               renderer.fillColor(Color(0.24f, 0.30f, 0.34f, 1.0f));
               renderer.rect(menuX + 5.0f, rowY, kMenuWidth - 10.0f, kRowHeight);
               renderer.fill();
            }
            drawText(renderer, menuX + 11.0f, textBaselineFromTop(renderer, rowY + 4.0f),
                     types[i].displayName.c_str(), UITheme::kTextPrimary, 12.0f);
         }
      }

      void ModuleCanvas::render(Renderer2D& renderer, int viewWidth, int viewHeight)
      {
         Lock lock(mMutex);
         float canvasTop = kTitleBarHeight;
         float canvasHeight = static_cast<float>(viewHeight) - canvasTop;

         // Canvas background with grid
         renderer.fillColor(Color(0.09f, 0.09f, 0.1f, 1.0f));
         renderer.rect(0, canvasTop, static_cast<float>(viewWidth), canvasHeight);
         renderer.fill();

         // Draw grid dots
         float gridSize = 50.0f * mScale;
         float startGridX = fmodf(mOffsetX * mScale, gridSize);
         float startGridY = fmodf(mOffsetY * mScale + canvasTop, gridSize);

         renderer.fillColor(Color(0.2f, 0.2f, 0.22f, 0.5f));
         for (float gx = startGridX; gx < viewWidth; gx += gridSize)
         {
            for (float gy = canvasTop + startGridY; gy < viewHeight; gy += gridSize)
            {
               renderer.circle(gx, gy, 1.0f);
               renderer.fill();
            }
         }

         // Draw connections (cables)
         for (const auto& conn : mConnections)
         {
            Module* srcMod = getModule(conn.sourceModuleId);
            Module* dstMod = getModule(conn.destModuleId);
            if (!srcMod || !dstMod)
               continue;

            // Calculate port positions
            float srcX = (srcMod->getX() + srcMod->getWidth() + mOffsetX) * mScale;
            float srcY = (srcMod->getY() + Module::kTitleBarHeight + 10 +
                          conn.sourcePortIndex * 15 + mOffsetY) *
                         mScale +
                         canvasTop;
            float dstX = (dstMod->getX() + mOffsetX) * mScale;
            float dstY = (dstMod->getY() + Module::kTitleBarHeight + 10 +
                          conn.destPortIndex * 15 + mOffsetY) *
                         mScale +
                         canvasTop;

            renderer.drawCableWithSag(srcX, srcY, dstX, dstY, conn.color, 2.5f * mScale, 0.2f);
         }

         // Draw in-progress connection
         if (mIsConnecting)
         {
            Module* srcMod = getModule(mConnectSourceId);
            if (srcMod)
            {
               float srcX = (srcMod->getX() + srcMod->getWidth() + mOffsetX) * mScale;
               float srcY = (srcMod->getY() + Module::kTitleBarHeight + 10 +
                             mConnectSourcePort * 15 + mOffsetY) *
                            mScale +
                            canvasTop;
               const Color previewColor = mConnectTargetCompatible
                                          ? Color(0.7f, 0.7f, 0.75f, 0.75f)
                                          : Color(0.95f, 0.2f, 0.2f, 0.85f);
               renderer.drawCableWithSag(srcX, srcY, mConnectEndX, mConnectEndY,
                                         previewColor, 2.0f, 0.15f);
            }
         }

         // Draw modules
         for (auto& [id, module] : mModules)
         {
            module->render(renderer, mOffsetX, mOffsetY + canvasTop / mScale, mScale);
         }

         // Render title bar on top
         renderTitleBar(renderer, viewWidth);
         renderSpawnMenu(renderer, viewWidth, viewHeight);

         // Docked live analyzer: its data is copied from the audio callback's lock-free ring.
         std::array<float, 256> waveform{};
         std::array<float, AudioAnalysis::kSpectrumBins> spectrum{};
         AudioAnalysis::copyLatest(waveform.data(), static_cast<int>(waveform.size()));
         AudioAnalysis::computeSpectrum(spectrum.data(), static_cast<int>(spectrum.size()));
         const float analyzerX = static_cast<float>(viewWidth) - 210.0f;
         const float analyzerY = static_cast<float>(viewHeight) - 135.0f;
         renderer.drawPanel(analyzerX, analyzerY, 200.0f, 105.0f, true);
         drawText(renderer, analyzerX + 8.0f, textBaselineFromTop(renderer, analyzerY + 5.0f),
                  "ANALYZER", UITheme::kTextSecondary, 9.0f);
         renderer.drawWaveform(analyzerX + 8.0f, analyzerY + 20.0f, 184.0f, 34.0f,
                               waveform.data(), static_cast<int>(waveform.size()), false);
         renderer.drawSpectrum(analyzerX + 8.0f, analyzerY + 61.0f, 184.0f, 35.0f,
                               spectrum.data(), static_cast<int>(spectrum.size()));

         // Draw zoom indicator (right-aligned)
         renderer.fontSize(10.0f);
         char zoomText[32];
         snprintf(zoomText, sizeof(zoomText), "%.0f%%", mScale * 100.0f);
         const float zoomW = renderer.textWidth(zoomText);
         drawText(renderer, static_cast<float>(viewWidth) - zoomW - 10.0f,
                  textBaselineFromTop(renderer, static_cast<float>(viewHeight) - 18.0f),
                  zoomText, Color(0.55f, 0.55f, 0.60f, 0.85f), 10.0f);

         // Status line
         const bool playing = mTransport && mTransport->isPlaying();
         const float bpm = mTransport ? mTransport->getBPM() : 120.0f;
         char countText[128];
         snprintf(countText, sizeof(countText),
                  "Modules: %d | Cables: %d | %s | %.1f BPM | Tab: demo panels",
                  static_cast<int>(mModules.size()),
                  static_cast<int>(mConnections.size()),
                  playing ? "Playing" : "Stopped",
                  bpm);
         drawText(renderer, 10.0f, textBaselineFromTop(renderer, static_cast<float>(viewHeight) - 18.0f),
                  countText, UITheme::kTextSecondary, 10.0f);
      }


   } // namespace wasm
} // namespace bespoke
