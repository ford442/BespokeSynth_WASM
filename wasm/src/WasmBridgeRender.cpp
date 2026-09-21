/**
 * BespokeSynth WASM - Render C ABI
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmBridge.h"
#include "BespokeWasm/WasmBridgeInternal.h"
#include "BespokeWasm/Renderer2D.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/PixelFont.h"
#include "BespokeWasm/SdfFont.h"
#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/WasmRuntimeState.h"

#include <atomic>
#include <cstdio>
#include <vector>
#include <emscripten.h>

using namespace bespoke::wasm;

namespace bespoke
{
   namespace wasm
   {
      std::atomic<uint32_t> gHostRenderCallCount{ 0 };
      std::atomic<uint32_t> gHostMouseDownCallCount{ 0 };
      float gFrameDeltaSeconds = 1.0f / 60.0f;
   } // namespace wasm
} // namespace bespoke

static float fontTestBaselineFromTop(float topY, float textHeight)
{
   return topY + textHeight * kPixelFontBaselineRatio;
}

static void renderFontTestPanel()
{
   auto& state = runtimeState();
   if (!state.renderer)
      return;

   auto& renderer = *state.renderer;
   const float panelX = 16.0f;
   const float panelY = 52.0f;
   const float panelW = static_cast<float>(state.width) - 32.0f;
   const float panelH = static_cast<float>(state.height) - 84.0f;
   const float panelBottom = panelY + panelH;

   renderer.fillColor(Color(0.06f, 0.06f, 0.08f, 0.94f));
   renderer.roundedRect(panelX, panelY, panelW, panelH, 8.0f);
   renderer.fill();

   renderer.strokeColor(Color(0.35f, 0.35f, 0.42f, 1.0f));
   renderer.strokeWidth(1.0f);
   renderer.roundedRect(panelX, panelY, panelW, panelH, 8.0f);
   renderer.stroke();

   const char* kSharp = "C\xe2\x99\xaf";
   const char* kFlat = "B\xe2\x99\xad";
   char musicalLine[64];
   snprintf(musicalLine, sizeof(musicalLine), "Musical: %smaj  %smin  A#  Db", kSharp, kFlat);

   const char* rows[] = {
      "Font Test Panel",
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
      "abcdefghijklmnopqrstuvwxyz",
      "0123456789  !@#$%^&*()-_=+<>?/\\|~",
      "In Out Pitch Gate Cutoff Resonance",
      "440 Hz  -12 dB  50%  1.50x",
      musicalLine,
   };

   // The regression panel always exercises the 5x7 bitmap font, even when the
   // rest of the UI draws with the SDF atlas.
   renderer.fontFace(kPixelFontFaceName);
   renderer.fontSize(10.0f);
   const float glyphRowH = renderer.textHeight() + 4.0f;
   const int glyphRows = 6;
   const float glyphSectionH = 12.0f + static_cast<float>(glyphRows) * glyphRowH;

   float rowTop = panelY + 14.0f;
   for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
   {
      const float rowFont = (i == 0) ? 13.0f : 10.0f;
      renderer.fillColor(i == 0 ? UITheme::kTextValue : UITheme::kTextSecondary);
      renderer.fontSize(rowFont);
      const float rowH = renderer.textHeight() + 5.0f;
      if (rowTop + rowH + glyphSectionH > panelBottom)
         break;

      renderer.text(panelX + 12.0f, fontTestBaselineFromTop(rowTop, renderer.textHeight()), rows[i]);
      rowTop += rowH;
   }

   renderer.fillColor(UITheme::kTextSecondary);
   renderer.fontSize(10.0f);
   float gx = panelX + 12.0f;
   float gy = fontTestBaselineFromTop(rowTop + 8.0f, renderer.textHeight());
   for (int c = 32; c <= 126; ++c)
   {
      char ch[2] = { static_cast<char>(c), '\0' };
      const float advance = renderer.textWidth(ch) + 2.0f;
      renderer.text(gx, gy, ch);
      gx += advance;
      if (gx > panelX + panelW - 18.0f)
      {
         gx = panelX + 12.0f;
         const float nextTop = gy + glyphRowH - renderer.textHeight() * kPixelFontBaselineRatio;
         if (nextTop + glyphRowH > panelBottom)
            break;
         gy = fontTestBaselineFromTop(nextTop, renderer.textHeight());
      }
   }
   renderer.fontFace("");
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void bespoke_set_frame_delta(float deltaSeconds)
{
   if (deltaSeconds > 0.0f && deltaSeconds < 1.0f)
      gFrameDeltaSeconds = deltaSeconds;
}

EMSCRIPTEN_KEEPALIVE void bespoke_reset_host_counters(void)
{
   gHostRenderCallCount.store(0, std::memory_order_relaxed);
   gHostMouseDownCallCount.store(0, std::memory_order_relaxed);
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_host_render_count(void)
{
   return static_cast<int>(gHostRenderCallCount.load(std::memory_order_relaxed));
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_host_mouse_down_count(void)
{
   return static_cast<int>(gHostMouseDownCallCount.load(std::memory_order_relaxed));
}

EMSCRIPTEN_KEEPALIVE void bespoke_render(void)
{
   auto& state = runtimeState();
   if (state.initState != InitState::FullyInitialized)
   {
      if (state.context)
         state.context->processEvents();
      return;
   }

   if (!state.renderer)
      return;

   if (state.context && state.context->isDeviceLost())
      return;

   gHostRenderCallCount.fetch_add(1, std::memory_order_relaxed);
   state.renderTime += gFrameDeltaSeconds;
   state.renderer->beginFrame(state.width, state.height,
                              static_cast<float>(emscripten_get_device_pixel_ratio()),
                              state.renderTime);

   state.renderer->fillColor(Color(0.12f, 0.12f, 0.14f, 1.0f));
   state.renderer->rect(0, 0, static_cast<float>(state.width), static_cast<float>(state.height));
   state.renderer->fill();

   const bool useLegacy = state.legacyPanelsEnabled && state.viewMode != 0;
   if (!useLegacy && state.canvas)
   {
      if (state.audioBackend)
      {
         const bool playing = state.canvas->isTransportPlaying();
         if (playing && !state.audioBackend->isRunning())
            state.audioBackend->start();
         else if (!playing && state.audioBackend->isRunning())
            state.audioBackend->stop();

         state.canvas->setOutputLevel(state.audioBackend->getOutputLevel());
      }
      state.canvas->render(*state.renderer, state.width, state.height);
   }
   else if (useLegacy)
   {
      renderDemoPanels();
   }

   if (state.fontTestVisible)
      renderFontTestPanel();

   state.renderer->endFrame();
}

EMSCRIPTEN_KEEPALIVE void bespoke_resize(int width, int height)
{
   auto& state = runtimeState();
   state.width = width;
   state.height = height;

   if (state.context)
      state.context->resize(width, height);
   if (state.webGL2Context)
      state.webGL2Context->resize(width, height);

   printf("BespokeSynth WASM: Resized to %dx%d\n", width, height);
}

EMSCRIPTEN_KEEPALIVE int bespoke_capture_screenshot(int* outWidth, int* outHeight)
{
   auto& state = runtimeState();
   if (!state.initialized || !state.renderer)
      return -1;

   state.screenshotPixels.clear();
   state.screenshotWidth = 0;
   state.screenshotHeight = 0;

   gBespokePendingScreenshotCapture = true;
   bespoke_render();
   gBespokePendingScreenshotCapture = false;

   if (outWidth)
      *outWidth = state.width;
   if (outHeight)
      *outHeight = state.height;

   if (state.rendererBackend == RendererBackendType::WebGL2)
      return 1;

   if (state.context)
   {
      std::vector<uint8_t> pixels;
      int w = 0;
      int h = 0;
      if (state.context->readCapturedPixels(pixels, w, h))
      {
         state.screenshotPixels = std::move(pixels);
         state.screenshotWidth = w;
         state.screenshotHeight = h;
         if (outWidth)
            *outWidth = w;
         if (outHeight)
            *outHeight = h;
         return 0;
      }
   }

   return -1;
}

EMSCRIPTEN_KEEPALIVE const unsigned char* bespoke_get_screenshot_pixels(int* outByteLength)
{
   auto& state = runtimeState();
   if (outByteLength)
      *outByteLength = static_cast<int>(state.screenshotPixels.size());
   return state.screenshotPixels.empty() ? nullptr : state.screenshotPixels.data();
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_render_test_mode(int scene)
{
   auto& state = runtimeState();
   state.renderTestMode = scene != 0;
   state.renderTestScene = scene;
   if (!state.canvas)
      return;

   state.viewMode = 0;
   if (scene == 2)
      state.canvas->setupDrumRenderTestScene();
   else
      state.canvas->setupCanonicalRenderTestScene();
   state.oscillatorModuleId = state.canvas->findFirstModuleOfType("oscillator");
   state.gainModuleId = state.canvas->findFirstModuleOfType("gain");
   if (state.renderTestMode)
      state.fontTestVisible = false;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_render_test_mode(void)
{
   const auto& state = runtimeState();
   return state.renderTestMode ? state.renderTestScene : 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_font_test_visible(int visible)
{
   runtimeState().fontTestVisible = visible != 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_font_test_visible(void)
{
   return runtimeState().fontTestVisible ? 1 : 0;
}

} // extern "C"
