/**
 * BespokeSynth WASM - Optional Mixer/Effects/Sequencer demo panels.
 * Compiled when BESPOKE_WASM_LEGACY_PANELS is on (off by default). Runtime-gated by
 * bespoke_set_legacy_panels() / ?legacyPanels=1.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmBridge.h"
#include "BespokeWasm/WasmBridgeInternal.h"
#include "BespokeWasm/Knob.h"
#include "BespokeWasm/Renderer2D.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/WasmRuntimeState.h"
#include "BespokeWasm/WidgetPainter.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <emscripten.h>

using namespace bespoke::wasm;

namespace bespoke
{
   namespace wasm
   {
      const char* legacyPanelName(int panelIndex)
      {
         static const char* panelNames[] = { "Mixer", "Effects", "Sequencer" };
         if (panelIndex >= 0 && panelIndex < kPanelCount)
            return panelNames[panelIndex];
         return "Unknown";
      }

      void logLegacyPanelStatus(int panelIndex, const char* action)
      {
         if (panelIndex < 0 || panelIndex >= kPanelCount)
            return;
         auto& status = runtimeState().panelStatus[panelIndex];
         printf("DEBUG [Panel:%s] %s - Loaded:%s Running:%s Frames:%d\n",
                legacyPanelName(panelIndex), action,
                status.loaded ? "YES" : "NO",
                status.running ? "YES" : "NO",
                status.frameCount);
      }

#if defined(BESPOKE_WASM_LEGACY_PANELS)

      void ensureLegacyDemoKnobs()
      {
         auto& state = runtimeState();
         if (!state.legacyPanelsEnabled || !state.knobs.empty())
            return;

         auto knob1 = std::make_unique<Knob>("Frequency", 0.5f);
         knob1->setRange(100.0f, 900.0f);
         knob1->setUnit("Hz");
         knob1->setDisplayPrecision(0);
         knob1->setStyle(KnobStyle::Classic);
         knob1->setColors(
         Color(0.25f, 0.25f, 0.28f, 1.0f),
         Color(0.7f, 0.7f, 0.75f, 1.0f),
         Color(0.4f, 0.8f, 0.5f, 1.0f));
         state.knobs.push_back(std::move(knob1));

         auto knob2 = std::make_unique<Knob>("Volume", 0.7f);
         knob2->setRange(0.0f, 1.0f);
         knob2->setUnit("%");
         knob2->setDisplayPrecision(0);
         knob2->setStyle(KnobStyle::Modern);
         knob2->setColors(
         Color(0.2f, 0.2f, 0.22f, 1.0f),
         Color(0.6f, 0.6f, 0.65f, 1.0f),
         Color(0.3f, 0.7f, 0.9f, 1.0f));
         state.knobs.push_back(std::move(knob2));

         auto knob3 = std::make_unique<Knob>("Filter", 0.3f);
         knob3->setRange(20.0f, 20000.0f);
         knob3->setUnit("Hz");
         knob3->setDisplayPrecision(0);
         knob3->setStyle(KnobStyle::LED);
         knob3->setColors(
         Color(0.15f, 0.15f, 0.18f, 1.0f),
         Color(0.5f, 0.5f, 0.55f, 1.0f),
         Color(0.9f, 0.4f, 0.2f, 1.0f));
         state.knobs.push_back(std::move(knob3));

         auto knob4 = std::make_unique<Knob>("Pan", 0.5f);
         knob4->setRange(-1.0f, 1.0f);
         knob4->setUnit("");
         knob4->setDisplayPrecision(2);
         knob4->setBipolar(true);
         knob4->setStyle(KnobStyle::Vintage);
         state.knobs.push_back(std::move(knob4));

         for (int i = 0; i < kPanelCount; i++)
         {
            state.panelStatus[i].loaded = true;
            state.panelStatus[i].running = false;
            state.panelStatus[i].frameCount = 0;
            logLegacyPanelStatus(i, "LOADED");
         }
      }

      void renderDemoPanels()
      {
         auto& state = runtimeState();
         if (!state.renderer)
            return;
         auto& renderer = *state.renderer;

         auto knobNormalized = [&state](int idx) -> float
         {
            if (idx < 0 || idx >= static_cast<int>(state.knobs.size()))
               return 0.5f;
            const auto& k = *state.knobs[idx];
            float range = k.getMax() - k.getMin();
            return (range > 0.0f) ? (k.getValue() - k.getMin()) / range : 0.5f;
         };

         renderer.fillColor(UITheme::kTextPrimary);
         renderer.fontSize(22.0f);
         renderer.text(20, 36, "BespokeSynth WASM");

         renderer.fillColor(UITheme::kAccentAmber);
         renderer.fontSize(11.0f);
         renderer.text(20, 56, "Legacy demo panels — Tab switches to Modular Canvas (recommended)");

         float tabY = 78.0f;
         float tabHeight = 35.0f;
         float tabWidth = 150.0f;
         float tabSpacing = 5.0f;
         const char* panelNames[] = { "Mixer", "Effects", "Sequencer" };

         for (int i = 0; i < kPanelCount; i++)
         {
            float tabX = 20.0f + static_cast<float>(i) * (tabWidth + tabSpacing);
            if (i == state.currentPanel)
               renderer.fillColor(Color(0.25f, 0.25f, 0.28f, 1.0f));
            else
               renderer.fillColor(Color(0.18f, 0.18f, 0.2f, 1.0f));
            renderer.roundedRect(tabX, tabY, tabWidth, tabHeight, 5.0f);
            renderer.fill();

            if (state.panelStatus[i].loaded && state.panelStatus[i].running)
               renderer.strokeColor(Color(0.3f, 0.8f, 0.4f, 1.0f));
            else if (i == state.currentPanel)
               renderer.strokeColor(Color(0.4f, 0.7f, 0.9f, 1.0f));
            else
               renderer.strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
            renderer.strokeWidth(2.0f);
            renderer.roundedRect(tabX, tabY, tabWidth, tabHeight, 5.0f);
            renderer.stroke();

            if (i == state.currentPanel)
               renderer.fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
            else
               renderer.fillColor(Color(0.6f, 0.6f, 0.65f, 1.0f));
            renderer.fontSize(14.0f);
            renderer.text(tabX + 15, tabY + 22, panelNames[i]);
         }

         float knobSize = 80.0f;
         float startX = 100.0f;
         float startY = 130.0f;
         float spacing = 120.0f;

         state.controlInfoCache.resize(state.knobs.size());
         for (size_t i = 0; i < state.knobs.size(); i++)
         {
            float x = startX + static_cast<float>(i) * spacing;
            state.knobs[i]->render(renderer, x, startY, knobSize);

            ControlInfoEntry& entry = state.controlInfoCache[i];
            entry.id = static_cast<int>(i);
            snprintf(entry.type, sizeof(entry.type), "knob");
            snprintf(entry.label, sizeof(entry.label), "%s", state.knobs[i]->getLabel().c_str());
            entry.value = state.knobs[i]->getValue();
            entry.min = state.knobs[i]->getMin();
            entry.max = state.knobs[i]->getMax();
            snprintf(entry.unit, sizeof(entry.unit), "%s", state.knobs[i]->getUnit().c_str());
            entry.x = x;
            entry.y = startY;
            entry.size = knobSize;
         }

         if (state.knobs.size() >= 2)
         {
            WidgetPainter(renderer).drawCableWithSag(
            startX, startY + knobSize * 0.5f + 20,
            startX + spacing, startY + knobSize * 0.5f + 20,
            Color(0.8f, 0.3f, 0.2f, 0.9f),
            3.0f, 0.2f);
         }
         if (state.knobs.size() >= 3)
         {
            WidgetPainter(renderer).drawCableWithSag(
            startX + spacing, startY + knobSize * 0.5f + 30,
            startX + spacing * 2, startY + knobSize * 0.5f + 30,
            Color(0.2f, 0.6f, 0.8f, 0.9f),
            3.0f, 0.25f);
         }

         float panelX = 50.0f;
         float panelY = 260.0f;
         float panelW = static_cast<float>(state.width) - 100.0f;
         float panelH = 220.0f;

         renderer.fillColor(Color(0.18f, 0.18f, 0.2f, 1.0f));
         renderer.roundedRect(panelX, panelY, panelW, panelH, 8.0f);
         renderer.fill();

         renderer.strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(panelX, panelY, panelW, panelH, 8.0f);
         renderer.stroke();

         renderer.fillColor(UITheme::kTextPrimary);
         renderer.fontSize(16.0f);
         renderer.text(panelX + 15, panelY + 25, panelNames[state.currentPanel]);

         switch (state.currentPanel)
         {
            case static_cast<int>(PanelType::Mixer):
            {
               float sliderX = panelX + 30;
               float sliderY = panelY + 50;
               float ch1Val = knobNormalized(1);
               float ch2Val = knobNormalized(3);
               float masterVal = (knobNormalized(0) * 0.4f + knobNormalized(1) * 0.6f);

               renderer.fillColor(UITheme::kTextPrimary);
               renderer.fontSize(12.0f);
               renderer.text(sliderX, sliderY - 10, "Channel 1");
               WidgetPainter(renderer).drawSlider(sliderX, sliderY, 200, 20, ch1Val,
                                                  UITheme::kBgTrack, UITheme::kAccentGreen);
               char vbuf[32];
               snprintf(vbuf, sizeof(vbuf), "%.0f%%", ch1Val * 100.0f);
               renderer.fillColor(UITheme::kTextValue);
               renderer.fontSize(10.0f);
               renderer.text(sliderX + 205, sliderY + 5, vbuf);

               renderer.fillColor(UITheme::kTextPrimary);
               renderer.fontSize(12.0f);
               renderer.text(sliderX, sliderY + 40, "Channel 2");
               WidgetPainter(renderer).drawSlider(sliderX, sliderY + 50, 200, 20, ch2Val,
                                                  UITheme::kBgTrack, UITheme::kAccentCyan);
               snprintf(vbuf, sizeof(vbuf), "%.0f%%", ch2Val * 100.0f);
               renderer.fillColor(UITheme::kTextValue);
               renderer.fontSize(10.0f);
               renderer.text(sliderX + 205, sliderY + 55, vbuf);

               renderer.fillColor(UITheme::kTextPrimary);
               renderer.fontSize(12.0f);
               renderer.text(sliderX, sliderY + 90, "Master");
               WidgetPainter(renderer).drawSlider(sliderX, sliderY + 100, 200, 20, masterVal,
                                                  UITheme::kBgTrack, UITheme::kAccentMagenta);
               snprintf(vbuf, sizeof(vbuf), "%.0f%%", masterVal * 100.0f);
               renderer.fillColor(UITheme::kTextValue);
               renderer.fontSize(10.0f);
               renderer.text(sliderX + 205, sliderY + 105, vbuf);

               float vuX = panelX + panelW - 120;
               float vuY = panelY + 40;
               float audioLevel = state.audioBackend ? state.audioBackend->getOutputLevel() : 0.0f;

               renderer.fillColor(Color(0.5f, 0.5f, 0.55f, 1.0f));
               renderer.text(vuX, vuY - 10, "L");
               WidgetPainter(renderer).drawVUMeter(vuX, vuY, 20, 160, audioLevel,
                                                   Color(0.2f, 0.8f, 0.3f, 1.0f),
                                                   Color(1.0f, 0.2f, 0.1f, 1.0f));
               renderer.text(vuX + 40, vuY - 10, "R");
               WidgetPainter(renderer).drawVUMeter(vuX + 40, vuY, 20, 160, audioLevel * 0.9f,
                                                   Color(0.2f, 0.8f, 0.3f, 1.0f),
                                                   Color(1.0f, 0.2f, 0.1f, 1.0f));
               break;
            }
            case static_cast<int>(PanelType::Effects):
            {
               float effectX = panelX + 30;
               float effectY = panelY + 50;
               float reverbMix = knobNormalized(2);
               float delayTime = knobNormalized(0);
               float chorusDepth = knobNormalized(1);
               float distortion = 1.0f - knobNormalized(2);

               renderer.fillColor(UITheme::kTextPrimary);
               renderer.fontSize(12.0f);
               renderer.text(effectX, effectY - 10, "Reverb Mix");
               WidgetPainter(renderer).drawSlider(effectX, effectY, 250, 20, reverbMix, UITheme::kBgTrack, UITheme::kAccentMagenta);
               {
                  char v[32];
                  snprintf(v, sizeof(v), "%.0f%%", reverbMix * 100.0f);
                  renderer.fillColor(UITheme::kTextValue);
                  renderer.fontSize(10.0f);
                  renderer.text(effectX + 255, effectY + 5, v);
               }

               renderer.fillColor(UITheme::kTextPrimary);
               renderer.fontSize(12.0f);
               renderer.text(effectX, effectY + 40, "Delay Time");
               WidgetPainter(renderer).drawSlider(effectX, effectY + 50, 250, 20, delayTime, UITheme::kBgTrack, UITheme::kAccentAmber);
               {
                  char v[32];
                  snprintf(v, sizeof(v), "%.0f ms", delayTime * 1000.0f);
                  renderer.fillColor(UITheme::kTextValue);
                  renderer.fontSize(10.0f);
                  renderer.text(effectX + 255, effectY + 55, v);
               }

               renderer.fillColor(UITheme::kTextPrimary);
               renderer.fontSize(12.0f);
               renderer.text(effectX, effectY + 90, "Chorus Depth");
               WidgetPainter(renderer).drawSlider(effectX, effectY + 100, 250, 20, chorusDepth, UITheme::kBgTrack, UITheme::kAccentCyan);
               {
                  char v[32];
                  snprintf(v, sizeof(v), "%.0f%%", chorusDepth * 100.0f);
                  renderer.fillColor(UITheme::kTextValue);
                  renderer.fontSize(10.0f);
                  renderer.text(effectX + 255, effectY + 105, v);
               }

               renderer.fillColor(UITheme::kTextPrimary);
               renderer.fontSize(12.0f);
               renderer.text(effectX, effectY + 140, "Distortion");
               WidgetPainter(renderer).drawSlider(effectX, effectY + 150, 250, 20, distortion, UITheme::kBgTrack, UITheme::kAccentMagenta);
               {
                  char v[32];
                  snprintf(v, sizeof(v), "%.0f%%", distortion * 100.0f);
                  renderer.fillColor(UITheme::kTextValue);
                  renderer.fontSize(10.0f);
                  renderer.text(effectX + 255, effectY + 155, v);
               }

               float vizX = panelX + panelW - 200;
               float vizY = panelY + 50;
               float vizW = 180;
               float vizH = 150;
               renderer.fillColor(Color(0.1f, 0.1f, 0.12f, 1.0f));
               renderer.rect(vizX, vizY, vizW, vizH);
               renderer.fill();
               renderer.strokeColor(Color(0.3f, 0.6f, 0.8f, 0.8f));
               renderer.strokeWidth(2.0f);
               for (int i = 0; i < 10; i++)
               {
                  float x1 = vizX + static_cast<float>(i) * vizW / 10;
                  float x2 = vizX + static_cast<float>(i + 1) * vizW / 10;
                  float y1 = vizY + vizH / 2 + sinf(static_cast<float>(i) * 0.5f) * 30;
                  float y2 = vizY + vizH / 2 + sinf(static_cast<float>(i + 1) * 0.5f) * 30;
                  renderer.line(x1, y1, x2, y2);
               }
               break;
            }
            case static_cast<int>(PanelType::Sequencer):
            {
               float seqX = panelX + 30;
               float seqY = panelY + 50;
               float bpmNorm = knobNormalized(0);
               float swingNorm = knobNormalized(3);
               float bpmDisplay = 60.0f + bpmNorm * 140.0f;

               renderer.fillColor(UITheme::kTextPrimary);
               renderer.fontSize(12.0f);
               renderer.text(seqX, seqY - 10, "BPM");
               WidgetPainter(renderer).drawSlider(seqX, seqY, 150, 20, bpmNorm,
                                                  Color(0.25f, 0.25f, 0.28f, 1.0f),
                                                  Color(0.5f, 0.8f, 0.4f, 1.0f));
               {
                  char v[32];
                  snprintf(v, sizeof(v), "%.0f BPM", bpmDisplay);
                  renderer.fillColor(UITheme::kTextValue);
                  renderer.fontSize(10.0f);
                  renderer.text(seqX + 155, seqY + 5, v);
               }

               renderer.fillColor(UITheme::kTextPrimary);
               renderer.fontSize(12.0f);
               renderer.text(seqX + 200, seqY - 10, "Swing");
               WidgetPainter(renderer).drawSlider(seqX + 200, seqY, 150, 20, swingNorm,
                                                  Color(0.25f, 0.25f, 0.28f, 1.0f),
                                                  Color(0.8f, 0.7f, 0.4f, 1.0f));
               {
                  char v[32];
                  snprintf(v, sizeof(v), "%.0f%%", swingNorm * 100.0f);
                  renderer.fillColor(UITheme::kTextValue);
                  renderer.fontSize(10.0f);
                  renderer.text(seqX + 355, seqY + 5, v);
               }

               float gridX = seqX;
               float gridY = seqY + 50;
               const float STEP_WIDTH = 35.0f;
               const float STEP_HEIGHT = 30.0f;
               const int NUM_STEPS = 16;
               const int NUM_ROWS = 4;
               const int STEP_PATTERN_INTERVAL = 3;
               renderer.text(gridX, gridY - 10, "Step Sequencer (16 steps x 4 notes)");
               for (int row = 0; row < NUM_ROWS; row++)
               {
                  for (int step = 0; step < NUM_STEPS; step++)
                  {
                     float sx = gridX + static_cast<float>(step) * STEP_WIDTH;
                     float sy = gridY + static_cast<float>(row) * STEP_HEIGHT;
                     bool active = (step + row) % STEP_PATTERN_INTERVAL == 0;
                     renderer.fillColor(active ? Color(0.4f, 0.7f, 0.5f, 1.0f)
                                               : Color(0.15f, 0.15f, 0.17f, 1.0f));
                     renderer.rect(sx, sy, STEP_WIDTH - 2, STEP_HEIGHT - 2);
                     renderer.fill();
                     renderer.strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
                     renderer.strokeWidth(1.0f);
                     renderer.rect(sx, sy, STEP_WIDTH - 2, STEP_HEIGHT - 2);
                     renderer.stroke();
                  }
               }
               break;
            }
         }

         renderer.fillColor(Color(0.6f, 0.6f, 0.65f, 1.0f));
         renderer.fontSize(12.0f);
         char statusText[256];
         const bool transportPlaying = state.canvas && state.canvas->isTransportPlaying();
         snprintf(statusText, sizeof(statusText),
                  "Sample rate: %d Hz | Buffer: %d | %s | Panel: %s | Tab: Modular Canvas",
                  bespoke_get_sample_rate(),
                  bespoke_get_buffer_size(),
                  transportPlaying ? "Playing" : "Stopped",
                  panelNames[state.currentPanel]);
         renderer.text(20, static_cast<float>(state.height) - 20, statusText);
         renderer.fillColor(Color(0.5f, 0.5f, 0.55f, 0.7f));
         renderer.fontSize(10.0f);
         renderer.text(static_cast<float>(state.width) - 150, 20, "Tab: Modular Canvas");
      }

#else

      void ensureLegacyDemoKnobs()
      {
      }

      void renderDemoPanels()
      {
      }

#endif
   } // namespace wasm
} // namespace bespoke

extern "C" {

EMSCRIPTEN_KEEPALIVE void bespoke_set_panel(int panelIndex)
{
   auto& state = runtimeState();
   if (!state.legacyPanelsEnabled)
      return;
   if (panelIndex >= 0 && panelIndex < kPanelCount)
   {
      int prevPanel = state.currentPanel;
      state.currentPanel = panelIndex;
      printf("DEBUG [API] Panel switch via bespoke_set_panel: From:%s To:%s\n",
             legacyPanelName(prevPanel), legacyPanelName(panelIndex));
      logLegacyPanelStatus(panelIndex, "ACTIVATED");
   }
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_panel(void)
{
   return runtimeState().currentPanel;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_panel_count(void)
{
   return runtimeState().legacyPanelsEnabled ? kPanelCount : 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_panel_name(int panelIndex)
{
   if (!runtimeState().legacyPanelsEnabled)
      return "";
   return legacyPanelName(panelIndex);
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_panel_loaded(int panelIndex)
{
   auto& state = runtimeState();
   if (!state.legacyPanelsEnabled || panelIndex < 0 || panelIndex >= kPanelCount)
      return 0;
   return state.panelStatus[panelIndex].loaded ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_is_panel_running(int panelIndex)
{
   auto& state = runtimeState();
   if (!state.legacyPanelsEnabled || panelIndex < 0 || panelIndex >= kPanelCount)
      return 0;
   return state.panelStatus[panelIndex].running ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_panel_frame_count(int panelIndex)
{
   auto& state = runtimeState();
   if (!state.legacyPanelsEnabled || panelIndex < 0 || panelIndex >= kPanelCount)
      return 0;
   return state.panelStatus[panelIndex].frameCount;
}

EMSCRIPTEN_KEEPALIVE void bespoke_log_all_panels_status(void)
{
   if (!runtimeState().legacyPanelsEnabled)
      return;
   printf("\n=== DEBUG: All Panels Status ===\n");
   for (int i = 0; i < kPanelCount; i++)
      logLegacyPanelStatus(i, "STATUS CHECK");
   printf("=== End Panel Status ===\n\n");
}

} // extern "C"
