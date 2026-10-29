/**
 * BespokeSynth WASM - Backend-agnostic synth UI widgets
 *
 * Knobs, sliders, cables, meters, scopes and the other composite controls, drawn once
 * over the Renderer2D primitives (paths, text, transforms and drawQuad(ShaderKind)).
 * WebGPU and WebGL2 only implement those primitives; tests record them (RecordingRenderer).
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/Renderer2D.h"

namespace bespoke
{
   namespace wasm
   {

      class WidgetPainter
      {
      public:
         explicit WidgetPainter(Renderer2D& renderer)
         : mRenderer(renderer)
         {
         }

         void drawKnob(float cx, float cy, float radius, float value, const Color& bgColor, const Color& fgColor);
         void drawWire(float x1, float y1, float x2, float y2, const Color& color, float thickness = 2.0f);
         void drawCableWithSag(float x1, float y1, float x2, float y2, const Color& color, float thickness = 3.0f,
                               float sag = 0.3f);
         void drawSlider(float x, float y, float w, float h, float value, const Color& bgColor, const Color& fgColor);
         void drawVUMeter(float x, float y, float w, float h, float level, const Color& lowColor, const Color& highColor);

         // The controls below take their base color from the renderer's current fill color.
         void drawButton(float x, float y, float w, float h, const char* label, bool pressed, bool hover);
         void drawToggle(float x, float y, float w, float h, bool state);
         void drawFader(float x, float y, float w, float h, float value);
         void drawModWheel(float x, float y, float w, float h, float value);
         void drawADSR(float x, float y, float w, float h, float a, float d, float s, float r);
         void drawWaveform(float x, float y, float w, float h, const float* data, int count, bool filled);
         void drawSpectrum(float x, float y, float w, float h, const float* data, int count);
         void drawScope(float x, float y, float w, float h, const float* data, int count);
         void drawPanel(float x, float y, float w, float h, bool bordered);
         void drawLED(float x, float y, float w, float h, bool on);
         void drawProgressBar(float x, float y, float w, float h, float value);

         void drawXYPad(float x, float y, float w, float h, float cx, float cy);
         void drawFilterResponse(float x, float y, float w, float h);
         /**
          * A magnitude response: `gainDb[i]` at evenly spaced (log-frequency) points, +/- `rangeDb`
          * full scale, over a panel with 0 dB and +/- rangeDb / 2 guide lines (EQ, filters).
          */
         void drawResponseCurve(float x, float y, float w, float h, const float* gainDb, int count, float rangeDb,
                                const Color& color);
         void drawLFOWaveform(float x, float y, float w, float h);
         void drawSequencerStep(float x, float y, float w, float h, bool active);
         void drawPianoKey(float x, float y, float w, float h, bool black, bool pressed);
         void drawCircularScope(float x, float y, float w, float h);

      private:
         Renderer2D& mRenderer;
      };

   } // namespace wasm
} // namespace bespoke
