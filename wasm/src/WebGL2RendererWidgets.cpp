/**
 * BespokeSynth WASM - WebGL2 Renderer: widgets
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WebGL2Renderer.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace bespoke {
namespace wasm {

namespace {

static const float PI = 3.14159265f;
static const float TWO_PI = 6.28318530f;

} // namespace

void WebGL2Renderer::drawQuad(float x, float y, float w, float h, GLPipelineKind pipeline)
{
   setPipeline(pipeline);
   float tx1 = x, ty1 = y;
   float tx2 = x + w, ty2 = y;
   float tx3 = x + w, ty3 = y + h;
   float tx4 = x, ty4 = y + h;
   transformPoint(tx1, ty1);
   transformPoint(tx2, ty2);
   transformPoint(tx3, ty3);
   transformPoint(tx4, ty4);

   const Color& color = mCurrentState.fillColor;
   pushVertex(tx1, ty1, 0.0f, 0.0f, color);
   pushVertex(tx2, ty2, 1.0f, 0.0f, color);
   pushVertex(tx3, ty3, 1.0f, 1.0f, color);
   pushVertex(tx1, ty1, 0.0f, 0.0f, color);
   pushVertex(tx3, ty3, 1.0f, 1.0f, color);
   pushVertex(tx4, ty4, 0.0f, 1.0f, color);
}

void WebGL2Renderer::drawSolidQuad(float x, float y, float w, float h, const Color& color)
{
   fillColor(color);
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
}

void WebGL2Renderer::drawKnob(float cx, float cy, float radius, float value, const Color& bgColor, const Color& fgColor)
{
   if (mDebugMode == WebGLDebugMode::SimplifiedModules)
   {
      fillColor(bgColor);
      circle(cx, cy, radius);
      fill();
      return;
   }

   const float size = radius * 2.0f;
   fillColor(bgColor);
   drawQuad(cx - radius, cy - radius, size, size, GLPipelineKind::KnobHighlight);

   fillColor(fgColor);
   drawQuad(cx - radius, cy - radius, size, size, GLPipelineKind::DialTicks);

   const float startA = 0.75f * PI;
   const float valA = startA + value * 1.5f * PI;
   const int arcSegs = 12;
   strokeColor(UITheme::kAccentCyan);
   strokeWidth(2.0f);
   for (int i = 0; i < arcSegs; ++i)
   {
      const float t0 = static_cast<float>(i) / arcSegs;
      const float t1 = static_cast<float>(i + 1) / arcSegs;
      const float a0 = startA + (valA - startA) * t0;
      const float a1 = startA + (valA - startA) * t1;
      const float r = radius * 0.88f;
      line(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r);
   }

   const float angle = startA + value * 1.5f * PI;
   const float ix = cx + cosf(angle) * radius * 0.35f;
   const float iy = cy + sinf(angle) * radius * 0.35f;
   const float ix2 = cx + cosf(angle) * radius * 0.82f;
   const float iy2 = cy + sinf(angle) * radius * 0.82f;
   strokeColor(fgColor);
   strokeWidth(2.2f);
   line(ix, iy, ix2, iy2);
   fillColor(UITheme::kKnobIndicator);
   circle(ix2, iy2, radius * 0.07f);
   fill();
}

void WebGL2Renderer::drawWire(float x1, float y1, float x2, float y2, const Color& color, float thickness)
{
   const float dx = x2 - x1;
   const float dy = y2 - y1;
   const float len = sqrtf(dx * dx + dy * dy);
   if (len < 0.0001f)
      return;

   save();
   translate(x1, y1);
   rotate(atan2f(dy, dx));
   fillColor(color);
   drawQuad(0.0f, -thickness * 2.0f, len, thickness * 4.0f, GLPipelineKind::WireGlow);
   drawQuad(0.0f, -thickness * 2.0f, len, thickness * 4.0f, GLPipelineKind::ConnectionPulse);
   restore();
}

void WebGL2Renderer::drawCableWithSag(float x1, float y1, float x2, float y2, const Color& color, float thickness, float sag)
{
   if (mDebugMode == WebGLDebugMode::ConnectionDebug)
   {
      fillColor(Color(1.0f, 1.0f, 0.0f, 1.0f));
      circle(x1, y1, 4.0f);
      fill();
      circle(x2, y2, 4.0f);
      fill();
   }

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

void WebGL2Renderer::drawSlider(float x, float y, float w, float h, float value, const Color& bgColor, const Color& fgColor)
{
   fillColor(bgColor);
   drawQuad(x, y, w, h, GLPipelineKind::SliderTrack);
   fillColor(fgColor);
   if (h > w)
   {
      const float fillH = h * value;
      drawQuad(x, y + h - fillH, w, fillH, GLPipelineKind::SliderFill);
      const float handleH = w * 0.5f;
      float handleY = y + h - fillH - handleH * 0.5f;
      if (handleY < y) handleY = y;
      if (handleY > y + h - handleH) handleY = y + h - handleH;
      drawQuad(x, handleY, w, handleH, GLPipelineKind::SliderHandle);
   }
   else
   {
      const float fillW = w * value;
      drawQuad(x, y, fillW, h, GLPipelineKind::SliderFill);
      const float handleW = h * 0.5f;
      float handleX = x + fillW - handleW * 0.5f;
      if (handleX < x) handleX = x;
      if (handleX > x + w - handleW) handleX = x + w - handleW;
      drawQuad(handleX, y, handleW, h, GLPipelineKind::SliderHandle);
   }
}

void WebGL2Renderer::drawVUMeter(float x, float y, float w, float h, float level, const Color& lowColor, const Color& highColor)
{
   fillColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
   rect(x, y, w, h);
   fill();

   const int numSegments = 10;
   const float segmentHeight = h / numSegments;
   const float gap = 2.0f;
   for (int i = 0; i < numSegments; ++i)
   {
      const float segmentLevel = static_cast<float>(i + 1) / numSegments;
      const float segmentY = y + h - (i + 1) * segmentHeight;
      if (segmentLevel <= level)
      {
         const float t = static_cast<float>(i) / numSegments;
         fillColor(Color(
            lowColor.r + (highColor.r - lowColor.r) * t,
            lowColor.g + (highColor.g - lowColor.g) * t,
            lowColor.b + (highColor.b - lowColor.b) * t,
            1.0f));
      }
      else
      {
         fillColor(Color(0.15f, 0.15f, 0.15f, 1.0f));
      }
      drawQuad(x + gap, segmentY + gap * 0.5f, w - gap * 2.0f, segmentHeight - gap, GLPipelineKind::VUMeter);
   }
}

void WebGL2Renderer::drawButton(float x, float y, float w, float h, const char* label, bool pressed, bool hover)
{
   Color baseColor = mCurrentState.fillColor;
   if (pressed)
   {
      baseColor.r *= 0.8f;
      baseColor.g *= 0.8f;
      baseColor.b *= 0.8f;
   }
   fillColor(baseColor);
   drawQuad(x, y, w, h, GLPipelineKind::Button);
   if (hover)
   {
      fillColor(Color(1.0f, 1.0f, 1.0f, 0.2f));
      drawQuad(x, y, w, h, GLPipelineKind::ButtonHover);
   }
   if (label && label[0])
   {
      fillColor(Color(1.0f, 1.0f, 1.0f, 0.9f));
      fontSize(11.0f);
      const float labelW = textWidth(label);
      text(x + (w - labelW) * 0.5f, y + h * 0.65f, label);
   }
}

void WebGL2Renderer::drawToggle(float x, float y, float w, float h, bool state)
{
   fillColor(mCurrentState.fillColor);
   drawQuad(x, y, w, h, GLPipelineKind::ToggleSwitch);
   const float thumbSize = h;
   const float thumbX = state ? (x + w - thumbSize) : x;
   fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
   drawQuad(thumbX, y, thumbSize, thumbSize, GLPipelineKind::ToggleThumb);
}

void WebGL2Renderer::drawFader(float x, float y, float w, float h, float value)
{
   fillColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
   drawQuad(x, y, w, h, GLPipelineKind::FaderGroove);
   const float capHeight = 30.0f;
   float capY = y + h - (h * value) - capHeight * 0.5f;
   if (capY < y) capY = y;
   if (capY > y + h - capHeight) capY = y + h - capHeight;
   fillColor(Color(0.8f, 0.8f, 0.85f, 1.0f));
   drawQuad(x - 5.0f, capY, w + 10.0f, capHeight, GLPipelineKind::FaderCap);
}

void WebGL2Renderer::drawModWheel(float x, float y, float w, float h, float value)
{
   (void)value;
   fillColor(mCurrentState.fillColor);
   drawQuad(x, y, w, h, GLPipelineKind::ModWheel);
}

void WebGL2Renderer::drawADSR(float x, float y, float w, float h, float a, float d, float s, float r)
{
   (void)a;
   (void)d;
   (void)s;
   (void)r;
   fillColor(Color(0.2f, 0.2f, 0.25f, 1.0f));
   drawQuad(x, y, w, h, GLPipelineKind::ADSRGrid);
}

void WebGL2Renderer::drawWaveform(float x, float y, float w, float h, const float* data, int count, bool filled)
{
   if (!data || count < 2)
      return;
   strokeColor(UITheme::kAccentCyan);
   strokeWidth(1.5f);
   beginPath();
   for (int i = 0; i < count; ++i)
   {
      const float px = x + (static_cast<float>(i) / (count - 1)) * w;
      const float py = y + h * 0.5f - data[i] * h * 0.45f;
      if (i == 0)
         moveTo(px, py);
      else
         lineTo(px, py);
   }
   stroke();
   if (filled)
   {
      lineTo(x + w, y + h);
      lineTo(x, y + h);
      closePath();
      fillColor(Color(0.2f, 0.7f, 0.9f, 0.25f));
      fill();
   }
}

void WebGL2Renderer::drawSpectrum(float x, float y, float w, float h, const float* data, int count)
{
   if (!data || count <= 0)
      return;
   const float barW = w / count;
   for (int i = 0; i < count; ++i)
   {
      const float barH = data[i] * h;
      const float barX = x + i * barW;
      const float barY = y + h - barH;
      fillColor(Color(0.0f, 1.0f, 0.0f, 1.0f));
      drawQuad(barX, barY, barW - 1.0f, barH, GLPipelineKind::SpectrumBar);
      drawQuad(barX, barY - 2.0f, barW - 1.0f, 2.0f, GLPipelineKind::SpectrumPeak);
   }
}

void WebGL2Renderer::drawScope(float x, float y, float w, float h, const float* data, int count)
{
   fillColor(Color(0.0f, 0.2f, 0.0f, 1.0f));
   drawQuad(x, y, w, h, GLPipelineKind::ScopeGrid);
   drawWaveform(x, y, w, h, data, count, false);
}

void WebGL2Renderer::drawPanel(float x, float y, float w, float h, bool bordered)
{
   fillColor(mCurrentState.fillColor);
   drawQuad(x, y, w, h, bordered ? GLPipelineKind::PanelBordered : GLPipelineKind::PanelBackground);
}

void WebGL2Renderer::drawLED(float x, float y, float w, float h, bool on)
{
   fillColor(mCurrentState.fillColor);
   drawQuad(x, y, w, h, on ? GLPipelineKind::LEDOn : GLPipelineKind::LEDOff);
}

void WebGL2Renderer::drawProgressBar(float x, float y, float w, float h, float value)
{
   fillColor(Color(0.2f, 0.2f, 0.2f, 1.0f));
   rect(x, y, w, h);
   fill();
   fillColor(mCurrentState.fillColor);
   drawQuad(x, y, w * value, h, GLPipelineKind::ProgressBar);
}

void WebGL2Renderer::drawXYPad(float x, float y, float w, float h, float cx, float cy)
{
   drawPanel(x, y, w, h, true);
   fillColor(UITheme::kAccentCyan);
   circle(x + cx * w, y + cy * h, 5.0f);
   fill();
}

void WebGL2Renderer::drawFilterResponse(float x, float y, float w, float h)
{
   drawPanel(x, y, w, h, true);
   strokeColor(UITheme::kAccentMagenta);
   strokeWidth(2.0f);
   beginPath();
   moveTo(x, y + h);
   for (int i = 0; i <= 32; ++i)
   {
      const float t = static_cast<float>(i) / 32.0f;
      const float px = x + t * w;
      const float py = y + h * (1.0f - (1.0f / (1.0f + t * t * 20.0f)));
      lineTo(px, py);
   }
   stroke();
}

void WebGL2Renderer::drawLFOWaveform(float x, float y, float w, float h)
{
   std::vector<float> data(64);
   for (int i = 0; i < 64; ++i)
      data[i] = sinf(static_cast<float>(i) / 64.0f * TWO_PI);
   drawWaveform(x, y, w, h, data.data(), 64, true);
}

void WebGL2Renderer::drawSequencerStep(float x, float y, float w, float h, bool active)
{
   fillColor(active ? UITheme::kAccentCyan : Color(0.2f, 0.2f, 0.22f, 1.0f));
   drawQuad(x, y, w, h, GLPipelineKind::Button);
}

void WebGL2Renderer::drawSpectrumWaterfall(float x, float y, float w, float h)
{
   drawPanel(x, y, w, h, true);
}

void WebGL2Renderer::drawPianoKey(float x, float y, float w, float h, bool black, bool pressed)
{
   fillColor(black ? Color(0.1f, 0.1f, 0.12f, 1.0f) : Color(0.9f, 0.9f, 0.92f, 1.0f));
   if (pressed)
      fillColor(UITheme::kAccentCyan);
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
}

void WebGL2Renderer::drawSpectrumRainbow(float x, float y, float w, float h, float* data, int count)
{
   drawSpectrum(x, y, w, h, data, count);
}

void WebGL2Renderer::drawCircularScope(float x, float y, float w, float h)
{
   drawPanel(x, y, w, h, true);
   strokeColor(UITheme::kAccentCyan);
   strokeWidth(2.0f);
   circle(x + w * 0.5f, y + h * 0.5f, std::min(w, h) * 0.35f);
   stroke();
}

void WebGL2Renderer::drawEchoTrail(float x, float y, float w, float h)
{
   drawPanel(x, y, w, h, true);
}

} // namespace wasm
} // namespace bespoke
