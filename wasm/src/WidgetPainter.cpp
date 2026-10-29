/**
 * BespokeSynth WASM - Backend-agnostic synth UI widgets
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WidgetPainter.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr float PI = 3.14159265f;
         constexpr float TWO_PI = 6.28318530f;
      }

      void WidgetPainter::drawKnob(float cx, float cy, float radius, float value, const Color& bgColor, const Color& fgColor)
      {
         Renderer2D& r = mRenderer;
         if (r.getDebugMode() == WebGLDebugMode::SimplifiedModules)
         {
            r.fillColor(bgColor);
            r.circle(cx, cy, radius);
            r.fill();
            return;
         }

         const float size = radius * 2.0f;
         r.fillColor(bgColor);
         r.drawQuad(ShaderKind::KnobHighlight, cx - radius, cy - radius, size, size);

         r.fillColor(fgColor);
         r.drawQuad(ShaderKind::DialTicks, cx - radius, cy - radius, size, size);

         // Value arc (range indicator), as short line segments.
         const float startA = 0.75f * PI;
         const float valA = startA + value * 1.5f * PI;
         const int arcSegs = 12;
         r.strokeColor(UITheme::kAccentCyan);
         r.strokeWidth(2.0f);
         for (int i = 0; i < arcSegs; ++i)
         {
            const float t0 = static_cast<float>(i) / arcSegs;
            const float t1 = static_cast<float>(i + 1) / arcSegs;
            const float a0 = startA + (valA - startA) * t0;
            const float a1 = startA + (valA - startA) * t1;
            const float arcR = radius * 0.88f;
            r.line(cx + cosf(a0) * arcR, cy + sinf(a0) * arcR, cx + cosf(a1) * arcR, cy + sinf(a1) * arcR);
         }

         // Indicator line + tip dot.
         const float angle = startA + value * 1.5f * PI;
         const float ix = cx + cosf(angle) * radius * 0.35f;
         const float iy = cy + sinf(angle) * radius * 0.35f;
         const float ix2 = cx + cosf(angle) * radius * 0.82f;
         const float iy2 = cy + sinf(angle) * radius * 0.82f;
         r.strokeColor(fgColor);
         r.strokeWidth(2.2f);
         r.line(ix, iy, ix2, iy2);
         r.fillColor(UITheme::kKnobIndicator);
         r.circle(ix2, iy2, radius * 0.07f);
         r.fill();
      }

      void WidgetPainter::drawWire(float x1, float y1, float x2, float y2, const Color& color, float thickness)
      {
         Renderer2D& r = mRenderer;
         const float dx = x2 - x1;
         const float dy = y2 - y1;
         const float len = sqrtf(dx * dx + dy * dy);
         if (len < 0.0001f)
            return;

         // A quad along the segment for the glow shader, plus the pulse on top.
         r.save();
         r.translate(x1, y1);
         r.rotate(atan2f(dy, dx));
         r.fillColor(color);
         r.drawQuad(ShaderKind::WireGlow, 0.0f, -thickness * 2.0f, len, thickness * 4.0f);
         r.drawQuad(ShaderKind::ConnectionPulse, 0.0f, -thickness * 2.0f, len, thickness * 4.0f);
         r.restore();
      }

      void WidgetPainter::drawCableWithSag(float x1, float y1, float x2, float y2, const Color& color, float thickness,
                                           float sag)
      {
         Renderer2D& r = mRenderer;
         if (r.getDebugMode() == WebGLDebugMode::ConnectionDebug)
         {
            r.fillColor(Color(1.0f, 1.0f, 0.0f, 1.0f));
            r.circle(x1, y1, 4.0f);
            r.fill();
            r.circle(x2, y2, 4.0f);
            r.fill();
         }

         // A quadratic bezier sagging below the midpoint, tessellated into glow segments.
         const int segments = 20;
         const float midX = (x1 + x2) * 0.5f;
         const float midY = (y1 + y2) * 0.5f;
         const float dist = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
         const float sagAmount = dist * sag;
         const float cx = midX;
         const float cy = midY + sagAmount;

         float prevX = x1;
         float prevY = y1;
         for (int i = 1; i <= segments; ++i)
         {
            const float t = static_cast<float>(i) / segments;
            const float inv = 1.0f - t;
            const float bx = inv * inv * x1 + 2.0f * inv * t * cx + t * t * x2;
            const float by = inv * inv * y1 + 2.0f * inv * t * cy + t * t * y2;
            drawWire(prevX, prevY, bx, by, color, thickness);
            prevX = bx;
            prevY = by;
         }
      }

      void WidgetPainter::drawSlider(float x, float y, float w, float h, float value, const Color& bgColor,
                                     const Color& fgColor)
      {
         Renderer2D& r = mRenderer;
         r.fillColor(bgColor);
         r.drawQuad(ShaderKind::SliderTrack, x, y, w, h);
         r.fillColor(fgColor);
         if (h > w)
         {
            const float fillH = h * value;
            r.drawQuad(ShaderKind::SliderFill, x, y + h - fillH, w, fillH);
            const float handleH = w * 0.5f;
            const float handleY = std::clamp(y + h - fillH - handleH * 0.5f, y, y + h - handleH);
            r.drawQuad(ShaderKind::SliderHandle, x, handleY, w, handleH);
         }
         else
         {
            const float fillW = w * value;
            r.drawQuad(ShaderKind::SliderFill, x, y, fillW, h);
            const float handleW = h * 0.5f;
            const float handleX = std::clamp(x + fillW - handleW * 0.5f, x, x + w - handleW);
            r.drawQuad(ShaderKind::SliderHandle, handleX, y, handleW, h);
         }
      }

      void WidgetPainter::drawVUMeter(float x, float y, float w, float h, float level, const Color& lowColor,
                                      const Color& highColor)
      {
         Renderer2D& r = mRenderer;
         r.fillColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
         r.rect(x, y, w, h);
         r.fill();

         const int numSegments = 10;
         const float segmentHeight = h / numSegments;
         const float gap = 2.0f;
         for (int i = 0; i < numSegments; ++i)
         {
            const float segmentLevel = static_cast<float>(i + 1) / numSegments;
            const float segmentY = y + h - static_cast<float>(i + 1) * segmentHeight;
            if (segmentLevel <= level)
            {
               const float t = static_cast<float>(i) / numSegments;
               r.fillColor(Color(lowColor.r + (highColor.r - lowColor.r) * t, lowColor.g + (highColor.g - lowColor.g) * t,
                                 lowColor.b + (highColor.b - lowColor.b) * t, 1.0f));
            }
            else
            {
               r.fillColor(Color(0.15f, 0.15f, 0.15f, 1.0f));
            }
            r.drawQuad(ShaderKind::VUMeter, x + gap, segmentY + gap * 0.5f, w - gap * 2.0f, segmentHeight - gap);
         }
      }

      void WidgetPainter::drawButton(float x, float y, float w, float h, const char* label, bool pressed, bool hover)
      {
         Renderer2D& r = mRenderer;
         Color baseColor = r.currentFillColor();
         if (pressed)
         {
            baseColor.r *= 0.8f;
            baseColor.g *= 0.8f;
            baseColor.b *= 0.8f;
         }
         r.fillColor(baseColor);
         r.drawQuad(ShaderKind::Button, x, y, w, h);
         if (hover)
         {
            r.fillColor(Color(1.0f, 1.0f, 1.0f, 0.2f));
            r.drawQuad(ShaderKind::ButtonHover, x, y, w, h);
         }
         if (label && label[0])
         {
            r.fillColor(Color(1.0f, 1.0f, 1.0f, 0.9f));
            r.fontSize(11.0f);
            const float labelW = r.textWidth(label);
            r.text(x + (w - labelW) * 0.5f, y + h * 0.65f, label);
         }
      }

      void WidgetPainter::drawToggle(float x, float y, float w, float h, bool state)
      {
         Renderer2D& r = mRenderer;
         r.fillColor(r.currentFillColor());
         r.drawQuad(ShaderKind::ToggleSwitch, x, y, w, h);
         const float thumbSize = h;
         const float thumbX = state ? (x + w - thumbSize) : x;
         r.fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
         r.drawQuad(ShaderKind::ToggleThumb, thumbX, y, thumbSize, thumbSize);
      }

      void WidgetPainter::drawFader(float x, float y, float w, float h, float value)
      {
         Renderer2D& r = mRenderer;
         r.fillColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
         r.drawQuad(ShaderKind::FaderGroove, x, y, w, h);
         const float capHeight = 30.0f;
         // Not std::clamp: a fader shorter than its cap pins the cap to the bottom.
         float capY = std::max(y + h - (h * value) - capHeight * 0.5f, y);
         capY = std::min(capY, y + h - capHeight);
         r.fillColor(Color(0.8f, 0.8f, 0.85f, 1.0f));
         r.drawQuad(ShaderKind::FaderCap, x - 5.0f, capY, w + 10.0f, capHeight);
      }

      void WidgetPainter::drawModWheel(float x, float y, float w, float h, float value)
      {
         (void)value;
         mRenderer.drawQuad(ShaderKind::ModWheel, x, y, w, h);
      }

      void WidgetPainter::drawADSR(float x, float y, float w, float h, float a, float d, float s, float r)
      {
         (void)a;
         (void)d;
         (void)s;
         (void)r;
         mRenderer.fillColor(Color(0.2f, 0.2f, 0.25f, 1.0f));
         mRenderer.drawQuad(ShaderKind::ADSRGrid, x, y, w, h);
      }

      void WidgetPainter::drawWaveform(float x, float y, float w, float h, const float* data, int count, bool filled)
      {
         Renderer2D& r = mRenderer;
         if (!data || count < 2)
            return;
         r.strokeColor(UITheme::kAccentCyan);
         r.strokeWidth(1.5f);
         r.beginPath();
         for (int i = 0; i < count; ++i)
         {
            const float px = x + (static_cast<float>(i) / static_cast<float>(count - 1)) * w;
            const float py = y + h * 0.5f - data[i] * h * 0.45f;
            if (i == 0)
               r.moveTo(px, py);
            else
               r.lineTo(px, py);
         }
         r.stroke();
         if (filled)
         {
            r.lineTo(x + w, y + h);
            r.lineTo(x, y + h);
            r.closePath();
            r.fillColor(Color(0.2f, 0.7f, 0.9f, 0.25f));
            r.fill();
         }
      }

      void WidgetPainter::drawSpectrum(float x, float y, float w, float h, const float* data, int count)
      {
         Renderer2D& r = mRenderer;
         if (!data || count <= 0)
            return;
         const float barW = w / static_cast<float>(count);
         for (int i = 0; i < count; ++i)
         {
            const float barH = data[i] * h;
            const float barX = x + static_cast<float>(i) * barW;
            const float barY = y + h - barH;
            r.fillColor(Color(0.0f, 1.0f, 0.0f, 1.0f)); // the shaders color by height
            r.drawQuad(ShaderKind::SpectrumBar, barX, barY, barW - 1.0f, barH);
            r.drawQuad(ShaderKind::SpectrumPeak, barX, barY - 2.0f, barW - 1.0f, 2.0f);
         }
      }

      void WidgetPainter::drawScope(float x, float y, float w, float h, const float* data, int count)
      {
         mRenderer.fillColor(Color(0.0f, 0.2f, 0.0f, 1.0f));
         mRenderer.drawQuad(ShaderKind::ScopeGrid, x, y, w, h);
         drawWaveform(x, y, w, h, data, count, false);
      }

      void WidgetPainter::drawPanel(float x, float y, float w, float h, bool bordered)
      {
         mRenderer.drawQuad(bordered ? ShaderKind::PanelBordered : ShaderKind::PanelBackground, x, y, w, h);
      }

      void WidgetPainter::drawLED(float x, float y, float w, float h, bool on)
      {
         mRenderer.drawQuad(on ? ShaderKind::LEDOn : ShaderKind::LEDOff, x, y, w, h);
      }

      void WidgetPainter::drawProgressBar(float x, float y, float w, float h, float value)
      {
         Renderer2D& r = mRenderer;
         const Color barColor = r.currentFillColor();
         r.fillColor(Color(0.2f, 0.2f, 0.2f, 1.0f));
         r.rect(x, y, w, h);
         r.fill();
         r.fillColor(barColor);
         r.drawQuad(ShaderKind::ProgressBar, x, y, w * value, h);
      }

      void WidgetPainter::drawXYPad(float x, float y, float w, float h, float cx, float cy)
      {
         drawPanel(x, y, w, h, true);
         mRenderer.fillColor(UITheme::kAccentCyan);
         mRenderer.circle(x + cx * w, y + cy * h, 5.0f);
         mRenderer.fill();
      }

      void WidgetPainter::drawFilterResponse(float x, float y, float w, float h)
      {
         Renderer2D& r = mRenderer;
         drawPanel(x, y, w, h, true);
         r.strokeColor(UITheme::kAccentMagenta);
         r.strokeWidth(2.0f);
         r.beginPath();
         r.moveTo(x, y + h);
         for (int i = 0; i <= 32; ++i)
         {
            const float t = static_cast<float>(i) / 32.0f;
            r.lineTo(x + t * w, y + h * (1.0f - (1.0f / (1.0f + t * t * 20.0f))));
         }
         r.stroke();
      }

      void WidgetPainter::drawResponseCurve(float x,
                                            float y,
                                            float w,
                                            float h,
                                            const float* gainDb,
                                            int count,
                                            float rangeDb,
                                            const Color& color)
      {
         Renderer2D& r = mRenderer;
         drawPanel(x, y, w, h, true);
         if (!gainDb || count < 2 || rangeDb <= 0.0f)
            return;
         const float mid = y + h * 0.5f;
         const float pixelsPerDb = h * 0.5f / rangeDb;
         r.strokeWidth(1.0f);
         r.strokeColor(Color(1.0f, 1.0f, 1.0f, 0.25f));
         r.beginPath();
         r.moveTo(x, mid);
         r.lineTo(x + w, mid);
         r.stroke();
         r.strokeColor(Color(1.0f, 1.0f, 1.0f, 0.1f));
         for (float guide : { -0.5f, 0.5f })
         {
            r.beginPath();
            r.moveTo(x, mid + guide * h * 0.5f);
            r.lineTo(x + w, mid + guide * h * 0.5f);
            r.stroke();
         }
         r.strokeColor(color);
         r.strokeWidth(1.5f);
         r.beginPath();
         for (int i = 0; i < count; ++i)
         {
            const float db = std::max(-rangeDb, std::min(rangeDb, gainDb[i]));
            const float px = x + w * static_cast<float>(i) / static_cast<float>(count - 1);
            const float py = mid - db * pixelsPerDb;
            if (i == 0)
               r.moveTo(px, py);
            else
               r.lineTo(px, py);
         }
         r.stroke();
      }

      void WidgetPainter::drawLFOWaveform(float x, float y, float w, float h)
      {
         float data[64];
         for (int i = 0; i < 64; ++i)
            data[i] = sinf(static_cast<float>(i) / 64.0f * TWO_PI);
         drawWaveform(x, y, w, h, data, 64, true);
      }

      void WidgetPainter::drawSequencerStep(float x, float y, float w, float h, bool active)
      {
         mRenderer.fillColor(active ? UITheme::kAccentCyan : Color(0.2f, 0.2f, 0.22f, 1.0f));
         mRenderer.drawQuad(ShaderKind::Button, x, y, w, h);
      }

      void WidgetPainter::drawPianoKey(float x, float y, float w, float h, bool black, bool pressed)
      {
         if (pressed)
            mRenderer.fillColor(UITheme::kAccentCyan);
         else
            mRenderer.fillColor(black ? Color(0.1f, 0.1f, 0.12f, 1.0f) : Color(0.9f, 0.9f, 0.92f, 1.0f));
         mRenderer.drawQuad(ShaderKind::Solid, x, y, w, h);
      }

      void WidgetPainter::drawCircularScope(float x, float y, float w, float h)
      {
         drawPanel(x, y, w, h, true);
         mRenderer.strokeColor(UITheme::kAccentCyan);
         mRenderer.strokeWidth(2.0f);
         mRenderer.circle(x + w * 0.5f, y + h * 0.5f, std::min(w, h) * 0.35f);
         mRenderer.stroke();
      }

   } // namespace wasm
} // namespace bespoke
