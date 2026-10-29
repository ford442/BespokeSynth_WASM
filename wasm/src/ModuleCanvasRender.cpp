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
#include "BespokeWasm/WidgetPainter.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>

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

      void ModuleCanvas::renderMidiOverlay(Renderer2D& renderer, int viewWidth, float canvasTop)
      {
         // CC badges under each mapped module, e.g. "CC74 cutoff".
         const float badgeFont = 8.0f * mScale;
         std::map<int, int> badgeRows;
         for (const auto& binding : mMidiCcMap)
         {
            const Module* module = getModule(binding.moduleId);
            if (!module)
               continue;
            const int row = badgeRows[binding.moduleId]++;
            char badge[48];
            snprintf(badge, sizeof(badge), "CC%d %.20s", binding.cc, binding.controlName.c_str());
            const float x = (module->getX() + mOffsetX) * mScale;
            const float y = (module->getY() + module->getHeight() + mOffsetY) * mScale + canvasTop +
                            (2.0f + static_cast<float>(row) * 10.0f) * mScale;
            drawText(renderer, x + 2.0f * mScale, textBaselineFromTop(y, badgeFont), badge,
                     Color(0.45f, 0.80f, 0.95f, 0.8f), badgeFont);
         }

         if (mMidiLearnModuleId < 0)
            return;
         const Module* target = getModule(mMidiLearnModuleId);
         if (!target)
            return;
         const float x = (target->getX() + mOffsetX) * mScale;
         const float y = (target->getY() + mOffsetY) * mScale + canvasTop;
         renderer.strokeColor(Color(1.0f, 0.62f, 0.15f, 1.0f));
         renderer.strokeWidth(2.0f);
         renderer.roundedRect(x - 3.0f, y - 3.0f, target->getWidth() * mScale + 6.0f,
                              target->getHeight() * mScale + 6.0f, 5.0f);
         renderer.stroke();

         char banner[128];
         snprintf(banner, sizeof(banner), "MIDI learn: %s > %s - move a controller (Esc cancels, Del clears)",
                  target->getName().c_str(), mMidiLearnControl.c_str());
         renderer.fontSize(11.0f);
         const float bannerW = renderer.textWidth(banner) + 16.0f;
         const float bannerX = std::max(6.0f, (static_cast<float>(viewWidth) - bannerW) * 0.5f);
         const float bannerY = canvasTop + 6.0f;
         renderer.fillColor(Color(0.25f, 0.16f, 0.05f, 0.92f));
         renderer.roundedRect(bannerX, bannerY, bannerW, 20.0f, 4.0f);
         renderer.fill();
         drawText(renderer, bannerX + 8.0f, textBaselineFromTop(renderer, bannerY + 4.0f), banner,
                  Color(1.0f, 0.78f, 0.45f, 1.0f), 11.0f);
      }

      void ModuleCanvas::renderHoverHelp(Renderer2D& renderer, int viewWidth, int viewHeight)
      {
         // About a third of a second of dwell before the tooltip shows, as on the desktop.
         constexpr int kDwellFrames = 20;
         constexpr float kFont = 10.0f;
         constexpr float kMaxWidth = 240.0f;
         constexpr float kPad = 6.0f;
         if (mHoverModuleId < 0 || ++mHoverFrames < kDwellFrames)
            return;
         const std::string text = hoverHelpText();
         if (text.empty())
            return;
         renderer.fontSize(kFont);
         const auto lines = wrapText(renderer, text, kMaxWidth, 6);
         float width = 0.0f;
         for (const auto& line : lines)
            width = std::max(width, renderer.textWidth(line.c_str()));
         const float lineStep = textLineSpacing(kFont, 1.0f);
         const float boxW = width + kPad * 2.0f;
         const float boxH = lineStep * static_cast<float>(lines.size()) + kPad * 2.0f - 4.0f;
         // Below-right of the pointer, flipped inside the view at the edges.
         float boxX = mLastMouseX + 14.0f;
         float boxY = mLastMouseY + 18.0f;
         if (boxX + boxW > static_cast<float>(viewWidth) - 4.0f)
            boxX = std::max(4.0f, mLastMouseX - boxW - 6.0f);
         if (boxY + boxH > static_cast<float>(viewHeight) - 4.0f)
            boxY = std::max(kTitleBarHeight, mLastMouseY - boxH - 6.0f);

         renderer.fillColor(Color(0.10f, 0.10f, 0.12f, 0.96f));
         renderer.roundedRect(boxX, boxY, boxW, boxH, 3.0f);
         renderer.fill();
         renderer.strokeColor(Color(0.42f, 0.42f, 0.48f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(boxX, boxY, boxW, boxH, 3.0f);
         renderer.stroke();
         for (size_t i = 0; i < lines.size(); ++i)
         {
            drawText(renderer, boxX + kPad, textBaselineForLine(boxY + kPad, static_cast<int>(i), kFont, 1.0f),
                     lines[i].c_str(), UITheme::kTextPrimary, kFont);
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
         for (float gx = startGridX; gx < static_cast<float>(viewWidth); gx += gridSize)
         {
            for (float gy = canvasTop + startGridY; gy < static_cast<float>(viewHeight); gy += gridSize)
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
                          static_cast<float>(conn.sourcePortIndex * 15) + mOffsetY) *
                         mScale +
                         canvasTop;
            float dstX = (dstMod->getX() + mOffsetX) * mScale;
            float dstY = (dstMod->getY() + Module::kTitleBarHeight + 10 +
                          static_cast<float>(conn.destPortIndex * 15) + mOffsetY) *
                         mScale +
                         canvasTop;

            WidgetPainter(renderer).drawCableWithSag(srcX, srcY, dstX, dstY, conn.color, 2.5f * mScale, 0.2f);
         }

         // Draw in-progress connection
         if (mIsConnecting)
         {
            Module* srcMod = getModule(mConnectSourceId);
            if (srcMod)
            {
               float srcX = (srcMod->getX() + srcMod->getWidth() + mOffsetX) * mScale;
               float srcY = (srcMod->getY() + Module::kTitleBarHeight + 10 +
                             static_cast<float>(mConnectSourcePort * 15) + mOffsetY) *
                            mScale +
                            canvasTop;
               const Color previewColor = mConnectTargetCompatible
                                          ? Color(0.7f, 0.7f, 0.75f, 0.75f)
                                          : Color(0.95f, 0.2f, 0.2f, 0.85f);
               WidgetPainter(renderer).drawCableWithSag(srcX, srcY, mConnectEndX, mConnectEndY,
                                                        previewColor, 2.0f, 0.15f);
            }
         }

         // Draw modules
         for (auto& [id, module] : mModules)
         {
            module->render(renderer, mOffsetX, mOffsetY + canvasTop / mScale, mScale);
         }

         renderMidiOverlay(renderer, viewWidth, canvasTop);

         // Render title bar on top
         renderTitleBar(renderer, viewWidth);
         renderSpawnMenu(renderer, viewWidth, viewHeight);
         renderHoverHelp(renderer, viewWidth, viewHeight);

         // Docked live analyzer, fed by the audio callback's lock-free ring. WebGPU runs the
         // FFT in a compute pass (analyzer_fft.wgsl); other backends use the CPU path here.
         const float analyzerX = static_cast<float>(viewWidth) - 210.0f;
         const float analyzerY = static_cast<float>(viewHeight) - 135.0f;
         WidgetPainter(renderer).drawPanel(analyzerX, analyzerY, 200.0f, 105.0f, true);
         drawText(renderer, analyzerX + 8.0f, textBaselineFromTop(renderer, analyzerY + 5.0f),
                  "ANALYZER", UITheme::kTextSecondary, 9.0f);

         const float scopeX = analyzerX + 8.0f;
         const float scopeY = analyzerY + 20.0f;
         const float spectrumY = analyzerY + 61.0f;
         const float analyzerW = 184.0f;
         if (!renderer.drawLiveWaveform(scopeX, scopeY, analyzerW, 34.0f, UITheme::kAccentCyan))
         {
            std::array<float, 256> waveform{};
            AudioAnalysis::copyLatest(waveform.data(), static_cast<int>(waveform.size()));
            renderer.strokeColor(UITheme::kAccentCyan);
            WidgetPainter(renderer).drawWaveform(scopeX, scopeY, analyzerW, 34.0f, waveform.data(),
                                                 static_cast<int>(waveform.size()), false);
         }
         if (!renderer.drawLiveSpectrum(scopeX, spectrumY, analyzerW, 35.0f))
         {
            std::array<float, AudioAnalysis::kSpectrumBins> spectrum{};
            AudioAnalysis::computeSpectrum(spectrum.data(), static_cast<int>(spectrum.size()));
            WidgetPainter(renderer).drawSpectrum(scopeX, spectrumY, analyzerW, 35.0f, spectrum.data(),
                                                 static_cast<int>(spectrum.size()));
         }

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
