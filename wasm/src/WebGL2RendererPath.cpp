/**
 * BespokeSynth WASM - WebGL2 Renderer: state, paths, shader quads and text
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WebGL2Renderer.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/PixelFont.h"
#include "BespokeWasm/SdfFont.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace bespoke {
namespace wasm {


namespace {

static const int kArcTessellationFactor = 4;
static const float PI = 3.14159265f;
static const float TWO_PI = 6.28318530f;
static const float HALF_PI = 1.57079632f;

} // namespace

void WebGL2Renderer::save()
{
   mStateStack.push_back(mCurrentState);
}

void WebGL2Renderer::restore()
{
   if (!mStateStack.empty())
   {
      mCurrentState = mStateStack.back();
      mStateStack.pop_back();
   }
}

void WebGL2Renderer::reset()
{
   mCurrentState.transform[0] = 1.0f;
   mCurrentState.transform[1] = 0.0f;
   mCurrentState.transform[2] = 0.0f;
   mCurrentState.transform[3] = 1.0f;
   mCurrentState.transform[4] = 0.0f;
   mCurrentState.transform[5] = 0.0f;
   mCurrentState.fillColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
   mCurrentState.strokeColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
   mCurrentState.strokeWidth = 1.0f;
   mCurrentState.hasScissor = false;
}

void WebGL2Renderer::translate(float x, float y)
{
   mCurrentState.transform[4] += x;
   mCurrentState.transform[5] += y;
}

void WebGL2Renderer::rotate(float angle)
{
   const float cs = cosf(angle);
   const float sn = sinf(angle);
   float t[6];
   std::memcpy(t, mCurrentState.transform, sizeof(t));
   mCurrentState.transform[0] = t[0] * cs + t[2] * sn;
   mCurrentState.transform[1] = t[1] * cs + t[3] * sn;
   mCurrentState.transform[2] = -t[0] * sn + t[2] * cs;
   mCurrentState.transform[3] = -t[1] * sn + t[3] * cs;
}

void WebGL2Renderer::scale(float x, float y)
{
   mCurrentState.transform[0] *= x;
   mCurrentState.transform[1] *= x;
   mCurrentState.transform[2] *= y;
   mCurrentState.transform[3] *= y;
}

void WebGL2Renderer::resetTransform()
{
   mCurrentState.transform[0] = 1.0f;
   mCurrentState.transform[1] = 0.0f;
   mCurrentState.transform[2] = 0.0f;
   mCurrentState.transform[3] = 1.0f;
   mCurrentState.transform[4] = 0.0f;
   mCurrentState.transform[5] = 0.0f;
}

void WebGL2Renderer::scissor(float x, float y, float w, float h)
{
   bool was = mCurrentState.hasScissor;
   bool same = was &&
      mCurrentState.scissor[0] == x && mCurrentState.scissor[1] == y &&
      mCurrentState.scissor[2] == w && mCurrentState.scissor[3] == h;
   if (!same)
   {
      // flush pending geometry that was recorded under previous clip state
      if (mCurrentBatchFirstVertex != static_cast<uint32_t>(mVertices.size()))
         flushBatch();
      mCurrentState.scissor[0] = x;
      mCurrentState.scissor[1] = y;
      mCurrentState.scissor[2] = w;
      mCurrentState.scissor[3] = h;
      mCurrentState.hasScissor = true;
   }
}

void WebGL2Renderer::resetScissor()
{
   if (mCurrentState.hasScissor)
   {
      if (mCurrentBatchFirstVertex != static_cast<uint32_t>(mVertices.size()))
         flushBatch();
      mCurrentState.hasScissor = false;
   }
}

void WebGL2Renderer::fillColor(const Color& color)
{
   mCurrentState.fillColor = color;
}

void WebGL2Renderer::strokeColor(const Color& color)
{
   mCurrentState.strokeColor = color;
}

void WebGL2Renderer::strokeWidth(float width)
{
   mCurrentState.strokeWidth = width;
}

void WebGL2Renderer::beginPath()
{
   mPathPoints.clear();
   mPathHasStart = false;
}

void WebGL2Renderer::moveTo(float x, float y)
{
   transformPoint(x, y);
   mPathStartX = x;
   mPathStartY = y;
   mPathX = x;
   mPathY = y;
   mPathHasStart = true;
}

void WebGL2Renderer::lineTo(float x, float y)
{
   transformPoint(x, y);
   if (mPathHasStart)
   {
      mPathPoints.push_back(mPathX);
      mPathPoints.push_back(mPathY);
      mPathPoints.push_back(x);
      mPathPoints.push_back(y);
   }
   mPathX = x;
   mPathY = y;
}

void WebGL2Renderer::closePath()
{
   if (mPathHasStart)
      lineTo(mPathStartX, mPathStartY);
}

void WebGL2Renderer::bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y)
{
   const int segments = 20;
   const float px = mPathX;
   const float py = mPathY;
   transformPoint(c1x, c1y);
   transformPoint(c2x, c2y);
   transformPoint(x, y);

   for (int i = 1; i <= segments; ++i)
   {
      const float t = static_cast<float>(i) / segments;
      const float t2 = t * t;
      const float t3 = t2 * t;
      const float mt = 1.0f - t;
      const float mt2 = mt * mt;
      const float mt3 = mt2 * mt;

      const float bx = mt3 * px + 3.0f * mt2 * t * c1x + 3.0f * mt * t2 * c2x + t3 * x;
      const float by = mt3 * py + 3.0f * mt2 * t * c1y + 3.0f * mt * t2 * c2y + t3 * y;

      mPathPoints.push_back(mPathX);
      mPathPoints.push_back(mPathY);
      mPathPoints.push_back(bx);
      mPathPoints.push_back(by);
      mPathX = bx;
      mPathY = by;
   }
}

void WebGL2Renderer::quadTo(float cx, float cy, float x, float y)
{
   const float c1x = mPathX + (2.0f / 3.0f) * (cx - mPathX);
   const float c1y = mPathY + (2.0f / 3.0f) * (cy - mPathY);
   const float c2x = x + (2.0f / 3.0f) * (cx - x);
   const float c2y = y + (2.0f / 3.0f) * (cy - y);
   bezierTo(c1x, c1y, c2x, c2y, x, y);
}

void WebGL2Renderer::arc(float cx, float cy, float r, float a0, float a1, int dir)
{
   transformPoint(cx, cy);
   float da = a1 - a0;
   if (dir == 1 && da < 0)
      da += TWO_PI;

   const int numSegments = std::max(3, static_cast<int>(std::abs(da) * r / kArcTessellationFactor));
   const float dAngle = da / static_cast<float>(numSegments);

   if (!mPathHasStart)
   {
      moveTo(cx + cosf(a0) * r, cy + sinf(a0) * r);
   }

   for (int i = 1; i <= numSegments; ++i)
   {
      const float angle = a0 + dAngle * static_cast<float>(i);
      const float x = cx + cosf(angle) * r;
      const float y = cy + sinf(angle) * r;
      mPathPoints.push_back(mPathX);
      mPathPoints.push_back(mPathY);
      mPathPoints.push_back(x);
      mPathPoints.push_back(y);
      mPathX = x;
      mPathY = y;
   }
}

void WebGL2Renderer::arcTo(float x1, float y1, float x2, float y2, float radius)
{
   (void)radius;
   transformPoint(x1, y1);
   transformPoint(x2, y2);
   lineTo(x1, y1);
   lineTo(x2, y2);
}

void WebGL2Renderer::fill()
{
   setPipeline(GLPipelineKind::Solid);
   if (mPathPoints.size() < 4)
      return;

   float cx = 0.0f;
   float cy = 0.0f;
   int numPoints = 0;
   for (size_t i = 0; i < mPathPoints.size(); i += 2)
   {
      cx += mPathPoints[i];
      cy += mPathPoints[i + 1];
      ++numPoints;
   }
   cx /= static_cast<float>(numPoints);
   cy /= static_cast<float>(numPoints);

   for (size_t i = 0; i + 3 < mPathPoints.size(); i += 2)
   {
      pushVertex(cx, cy, 0.0f, 0.0f, mCurrentState.fillColor);
      pushVertex(mPathPoints[i], mPathPoints[i + 1], 0.0f, 0.0f, mCurrentState.fillColor);
      pushVertex(mPathPoints[i + 2], mPathPoints[i + 3], 0.0f, 0.0f, mCurrentState.fillColor);
   }
}

void WebGL2Renderer::stroke()
{
   setPipeline(GLPipelineKind::Solid);
   for (size_t i = 0; i + 3 < mPathPoints.size(); i += 2)
   {
      const float x1 = mPathPoints[i];
      const float y1 = mPathPoints[i + 1];
      const float x2 = mPathPoints[i + 2];
      const float y2 = mPathPoints[i + 3];

      const float dx = x2 - x1;
      const float dy = y2 - y1;
      const float len = sqrtf(dx * dx + dy * dy);
      if (len < 0.0001f)
         continue;

      // Always emit as triangles (quads) for thick or thin strokes; our batch
      // is drawn with GL_TRIANGLES and solid/wire programs. Use at least 1px
      // for visibility of "thin" strokes/polylines.
      float w = mCurrentState.strokeWidth;
      if (w < 1.0f || mDebugMode == WebGLDebugMode::Wireframe)
         w = 1.0f;

      const float nx = -dy / len * w * 0.5f;
      const float ny = dx / len * w * 0.5f;
      pushVertex(x1 - nx, y1 - ny, 0.0f, 0.0f, mCurrentState.strokeColor);
      pushVertex(x1 + nx, y1 + ny, 0.0f, 0.0f, mCurrentState.strokeColor);
      pushVertex(x2 + nx, y2 + ny, 0.0f, 0.0f, mCurrentState.strokeColor);
      pushVertex(x1 - nx, y1 - ny, 0.0f, 0.0f, mCurrentState.strokeColor);
      pushVertex(x2 + nx, y2 + ny, 0.0f, 0.0f, mCurrentState.strokeColor);
      pushVertex(x2 - nx, y2 - ny, 0.0f, 0.0f, mCurrentState.strokeColor);
   }
}

void WebGL2Renderer::rect(float x, float y, float w, float h)
{
   if (mDebugMode == WebGLDebugMode::SimplifiedModules)
   {
      drawSolidQuad(x, y, w, h, mCurrentState.fillColor);
      return;
   }
   beginPath();
   moveTo(x, y);
   lineTo(x + w, y);
   lineTo(x + w, y + h);
   lineTo(x, y + h);
   closePath();
}

void WebGL2Renderer::roundedRect(float x, float y, float w, float h, float r)
{
   if (mDebugMode == WebGLDebugMode::SimplifiedModules)
   {
      drawSolidQuad(x, y, w, h, mCurrentState.fillColor);
      return;
   }
   r = std::min(r, std::min(w, h) * 0.5f);
   beginPath();
   moveTo(x + r, y);
   lineTo(x + w - r, y);
   arc(x + w - r, y + r, r, -HALF_PI, 0.0f, 0);
   lineTo(x + w, y + h - r);
   arc(x + w - r, y + h - r, r, 0.0f, HALF_PI, 0);
   lineTo(x + r, y + h);
   arc(x + r, y + h - r, r, HALF_PI, PI, 0);
   lineTo(x, y + r);
   arc(x + r, y + r, r, PI, PI * 1.5f, 0);
   closePath();
}

void WebGL2Renderer::circle(float cx, float cy, float r)
{
   beginPath();
   arc(cx, cy, r, 0.0f, TWO_PI, 0);
   closePath();
}

void WebGL2Renderer::ellipse(float cx, float cy, float rx, float ry)
{
   beginPath();
   const int segments = 32;
   for (int i = 0; i <= segments; ++i)
   {
      const float angle = (static_cast<float>(i) / segments) * TWO_PI;
      const float x = cx + cosf(angle) * rx;
      const float y = cy + sinf(angle) * ry;
      if (i == 0)
         moveTo(x, y);
      else
         lineTo(x, y);
   }
   closePath();
}

void WebGL2Renderer::line(float x1, float y1, float x2, float y2)
{
   beginPath();
   moveTo(x1, y1);
   lineTo(x2, y2);
   stroke();
}

void WebGL2Renderer::fontSize(float size)
{
   mFontSize = size;
}

void WebGL2Renderer::fontFace(const char* name)
{
   mFontName = name ? name : "";
}

void WebGL2Renderer::text(float x, float y, const char* string)
{
   if (!string || string[0] == '\0')
      return;

   if (sdfTextActive())
   {
      setPipeline(GLPipelineKind::SdfText);
      const Color textColor = mCurrentState.fillColor;
      uiSdfFont().layout(string, x, y, mFontSize, [&](const SdfGlyphQuad& q) {
         float tx1 = q.x0, ty1 = q.y0;
         float tx2 = q.x1, ty2 = q.y0;
         float tx3 = q.x1, ty3 = q.y1;
         float tx4 = q.x0, ty4 = q.y1;
         transformPoint(tx1, ty1);
         transformPoint(tx2, ty2);
         transformPoint(tx3, ty3);
         transformPoint(tx4, ty4);
         pushVertex(tx1, ty1, q.u0, q.v0, textColor);
         pushVertex(tx2, ty2, q.u1, q.v0, textColor);
         pushVertex(tx3, ty3, q.u1, q.v1, textColor);
         pushVertex(tx1, ty1, q.u0, q.v0, textColor);
         pushVertex(tx3, ty3, q.u1, q.v1, textColor);
         pushVertex(tx4, ty4, q.u0, q.v1, textColor);
      });
      return;
   }

   const float charHeight = mFontSize;
   const float charSpacing = mFontSize * kPixelFontCharSpacingRatio;
   const Color textColor = mCurrentState.fillColor;

   setPipeline(GLPipelineKind::PixelText);

   float currentX = x;
   const size_t len = strlen(string);
   for (size_t i = 0; i < len;)
   {
      const PixelFontGlyph glyph = pixelFontDecodeGlyph(string, i, len);
      if (glyph.byteLength == 0)
         break;

      i += glyph.byteLength;

      const float charWidth = pixelFontGlyphAdvance(glyph.index, mFontSize);
      if (glyph.index == 0)
      {
         currentX += charWidth + charSpacing;
         continue;
      }

      const float u0 = static_cast<float>(glyph.index);
      const float u1 = static_cast<float>(glyph.index) + 1.0f;
      const float x1 = currentX;
      const float y1 = y - charHeight * kPixelFontBaselineRatio;
      const float x2 = x1 + charWidth;
      const float y2 = y1 + charHeight;

      float tx1 = x1, ty1 = y1;
      float tx2 = x2, ty2 = y1;
      float tx3 = x2, ty3 = y2;
      float tx4 = x1, ty4 = y2;
      transformPoint(tx1, ty1);
      transformPoint(tx2, ty2);
      transformPoint(tx3, ty3);
      transformPoint(tx4, ty4);

      pushVertex(tx1, ty1, u0, 0.0f, textColor);
      pushVertex(tx2, ty2, u1, 0.0f, textColor);
      pushVertex(tx3, ty3, u1, 1.0f, textColor);
      pushVertex(tx1, ty1, u0, 0.0f, textColor);
      pushVertex(tx3, ty3, u1, 1.0f, textColor);
      pushVertex(tx4, ty4, u0, 1.0f, textColor);

      currentX += charWidth + charSpacing;
   }
}

float WebGL2Renderer::textWidth(const char* string)
{
   if (sdfTextActive())
      return uiSdfFont().textWidth(string, mFontSize);
   return pixelFontTextWidth(string, mFontSize);
}

float WebGL2Renderer::textHeight() const
{
   return pixelFontTextHeight(mFontSize);
}


namespace {

// ShaderKind (Renderer2D) -> the program that implements it.
constexpr std::array<GLPipelineKind, static_cast<size_t>(ShaderKind::Count)> kShaderPipelines = {
   GLPipelineKind::Solid,
   GLPipelineKind::KnobHighlight,
   GLPipelineKind::DialTicks,
   GLPipelineKind::WireGlow,
   GLPipelineKind::ConnectionPulse,
   GLPipelineKind::SliderTrack,
   GLPipelineKind::SliderFill,
   GLPipelineKind::SliderHandle,
   GLPipelineKind::Button,
   GLPipelineKind::ButtonHover,
   GLPipelineKind::ToggleSwitch,
   GLPipelineKind::ToggleThumb,
   GLPipelineKind::VUMeter,
   GLPipelineKind::ADSRGrid,
   GLPipelineKind::PanelBackground,
   GLPipelineKind::PanelBordered,
   GLPipelineKind::LEDOn,
   GLPipelineKind::LEDOff,
   GLPipelineKind::SpectrumBar,
   GLPipelineKind::SpectrumPeak,
   GLPipelineKind::ProgressBar,
   GLPipelineKind::ModWheel,
   GLPipelineKind::ScopeGrid,
   GLPipelineKind::FaderGroove,
   GLPipelineKind::FaderCap,
};

} // namespace

void WebGL2Renderer::drawQuad(ShaderKind kind, float x, float y, float w, float h)
{
   const size_t index = static_cast<size_t>(kind);
   drawQuad(x, y, w, h, index < kShaderPipelines.size() ? kShaderPipelines[index] : GLPipelineKind::Solid);
}

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

} // namespace wasm
} // namespace bespoke
