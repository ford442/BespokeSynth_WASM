/**
 * BespokeSynth WASM - WebGL2 Renderer Implementation
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "WebGL2Renderer.h"
#include "WebGL2Shaders.h"
#include "BespokeWasm/Theme.h"
#include "PixelFont.h"
#include <algorithm>
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

WebGL2Renderer::WebGL2Renderer(WebGL2Context& context)
: mContext(context)
{
   reset();
}

WebGL2Renderer::~WebGL2Renderer()
{
   if (mVertexBuffer)
      glDeleteBuffers(1, &mVertexBuffer);
   if (mVertexArray)
      glDeleteVertexArrays(1, &mVertexArray);
   for (GL2Program& prog : mPrograms)
   {
      if (prog.program)
         glDeleteProgram(prog.program);
   }
   if (mSharedVertexShader)
      glDeleteShader(mSharedVertexShader);
}

GLuint WebGL2Renderer::compileShader(GLenum type, const char* source)
{
   GLuint shader = glCreateShader(type);
   glShaderSource(shader, 1, &source, nullptr);
   glCompileShader(shader);

   GLint ok = 0;
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (!ok)
   {
      char log[1024];
      glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
      printf("WebGL2Renderer: shader compile error: %s\n", log);
      glDeleteShader(shader);
      return 0;
   }
   return shader;
}

GLuint WebGL2Renderer::linkProgram(GLuint vertexShader, GLuint fragmentShader)
{
   GLuint program = glCreateProgram();
   glAttachShader(program, vertexShader);
   glAttachShader(program, fragmentShader);
   glLinkProgram(program);

   GLint ok = 0;
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   if (!ok)
   {
      char log[1024];
      glGetProgramInfoLog(program, sizeof(log), nullptr, log);
      printf("WebGL2Renderer: program link error: %s\n", log);
      glDeleteProgram(program);
      return 0;
   }
   return program;
}

bool WebGL2Renderer::buildProgram(GLPipelineKind kind, const char* fragmentSrc, bool needsTime)
{
   GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
   if (!fs)
      return false;

   GLuint program = linkProgram(mSharedVertexShader, fs);
   glDeleteShader(fs);
   if (!program)
      return false;

   GL2Program& slot = mPrograms[static_cast<size_t>(kind)];
   slot.program = program;
   slot.uViewSize = glGetUniformLocation(program, "uViewSize");
   slot.uTime = needsTime ? glGetUniformLocation(program, "uTime") : -1;
   return true;
}

void WebGL2Renderer::createPrograms()
{
   mSharedVertexShader = compileShader(GL_VERTEX_SHADER, gl2VertexShaderSrc());
   if (!mSharedVertexShader)
      return;

   buildProgram(GLPipelineKind::Solid, gl2SolidFragmentSrc(), false);
   buildProgram(GLPipelineKind::KnobHighlight, gl2KnobHighlightFragmentSrc(), false);
   buildProgram(GLPipelineKind::DialTicks, gl2DialTicksFragmentSrc(), false);
   buildProgram(GLPipelineKind::WireGlow, gl2WireGlowFragmentSrc(), false);
   buildProgram(GLPipelineKind::ConnectionPulse, gl2ConnectionPulseFragmentSrc(), true);
   buildProgram(GLPipelineKind::SliderTrack, gl2SliderTrackFragmentSrc(), false);
   buildProgram(GLPipelineKind::SliderFill, gl2SliderFillFragmentSrc(), true);
   buildProgram(GLPipelineKind::SliderHandle, gl2SliderHandleFragmentSrc(), false);
   buildProgram(GLPipelineKind::Button, gl2ButtonFragmentSrc(), false);
   buildProgram(GLPipelineKind::ButtonHover, gl2ButtonHoverFragmentSrc(), true);
   buildProgram(GLPipelineKind::ToggleSwitch, gl2ToggleSwitchFragmentSrc(), false);
   buildProgram(GLPipelineKind::ToggleThumb, gl2ToggleThumbFragmentSrc(), false);
   buildProgram(GLPipelineKind::VUMeter, gl2VUMeterFragmentSrc(), false);
   buildProgram(GLPipelineKind::ADSRGrid, gl2ADSRGridFragmentSrc(), false);
   buildProgram(GLPipelineKind::ADSREnvelope, gl2ADSREnvelopeFragmentSrc(), false);
   buildProgram(GLPipelineKind::PanelBackground, gl2PanelBackgroundFragmentSrc(), false);
   buildProgram(GLPipelineKind::PanelBordered, gl2PanelBorderedFragmentSrc(), false);
   buildProgram(GLPipelineKind::LEDOn, gl2LEDOnFragmentSrc(), false);
   buildProgram(GLPipelineKind::LEDOff, gl2LEDOffFragmentSrc(), false);
   buildProgram(GLPipelineKind::Waveform, gl2WaveformFragmentSrc(), false);
   buildProgram(GLPipelineKind::WaveformFilled, gl2WaveformFilledFragmentSrc(), false);
   buildProgram(GLPipelineKind::SpectrumBar, gl2SpectrumBarFragmentSrc(), false);
   buildProgram(GLPipelineKind::SpectrumPeak, gl2SpectrumPeakFragmentSrc(), false);
   buildProgram(GLPipelineKind::ProgressBar, gl2ProgressBarFragmentSrc(), true);
   buildProgram(GLPipelineKind::ModWheel, gl2ModWheelFragmentSrc(), false);
   buildProgram(GLPipelineKind::ScopeGrid, gl2ScopeGridFragmentSrc(), false);
   buildProgram(GLPipelineKind::FaderGroove, gl2FaderGrooveFragmentSrc(), false);
   buildProgram(GLPipelineKind::FaderCap, gl2FaderCapFragmentSrc(), false);

   std::string pixelTextSrc = gl2PixelTextFragmentPrefix();
   pixelTextSrc += std::to_string(kPixelFontGlyphCount);
   pixelTextSrc += gl2PixelTextFragmentSuffix();
   buildProgram(GLPipelineKind::PixelText, pixelTextSrc.c_str(), false);

   const GLint fontColsLoc = glGetUniformLocation(programFor(GLPipelineKind::PixelText).program, "uFontCols");
   if (fontColsLoc >= 0)
   {
      const uint32_t* cols = getPixelFontColumns();
      std::vector<GLint> fontData(kPixelFontGlyphCount * kPixelFontColumnsPerGlyph);
      for (size_t i = 0; i < fontData.size(); ++i)
         fontData[i] = static_cast<GLint>(cols[i]);
      glUseProgram(programFor(GLPipelineKind::PixelText).program);
      glUniform1iv(fontColsLoc, static_cast<GLsizei>(fontData.size()), fontData.data());
   }
}

const WebGL2Renderer::GL2Program& WebGL2Renderer::programFor(GLPipelineKind kind) const
{
   return mPrograms[static_cast<size_t>(kind)];
}

void WebGL2Renderer::createBuffers()
{
   glGenVertexArrays(1, &mVertexArray);
   glGenBuffers(1, &mVertexBuffer);

   glBindVertexArray(mVertexArray);
   glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex2D) * 65536, nullptr, GL_DYNAMIC_DRAW);

   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<void*>(0));
   glEnableVertexAttribArray(1);
   glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<void*>(offsetof(Vertex2D, u)));
   glEnableVertexAttribArray(2);
   glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<void*>(offsetof(Vertex2D, color)));

   glBindVertexArray(0);
}

bool WebGL2Renderer::initialize()
{
   if (!mContext.isInitialized())
      return false;

   emscripten_webgl_make_context_current(mContext.getContextHandle());
   createPrograms();
   createBuffers();
   return programFor(GLPipelineKind::Solid).program != 0
      && programFor(GLPipelineKind::PixelText).program != 0;
}

void WebGL2Renderer::beginFrame(int width, int height, float pixelRatio, float time)
{
   mWidth = width;
   mHeight = height;
   mPixelRatio = pixelRatio;
   mTime = time;

   mVertices.clear();
   mDrawCalls.clear();
   mCurrentBatchFirstVertex = 0;
   mCurrentPipeline = GLPipelineKind::Solid;

   mContext.beginFrame();
   reset();
}

void WebGL2Renderer::flushBatch()
{
   const uint32_t batchCount = static_cast<uint32_t>(mVertices.size()) - mCurrentBatchFirstVertex;
   if (batchCount > 0)
   {
      DrawCall dc;
      dc.pipeline = mCurrentPipeline;
      dc.firstVertex = mCurrentBatchFirstVertex;
      dc.vertexCount = batchCount;
      dc.hasScissor = mCurrentState.hasScissor;
      if (dc.hasScissor)
      {
         dc.scissor[0] = mCurrentState.scissor[0];
         dc.scissor[1] = mCurrentState.scissor[1];
         dc.scissor[2] = mCurrentState.scissor[2];
         dc.scissor[3] = mCurrentState.scissor[3];
      }
      mDrawCalls.push_back(dc);
   }
   mCurrentBatchFirstVertex = static_cast<uint32_t>(mVertices.size());
}

void WebGL2Renderer::setPipeline(GLPipelineKind pipeline)
{
   if (mCurrentPipeline != pipeline)
   {
      flushBatch();
      mCurrentPipeline = pipeline;
   }
}

void WebGL2Renderer::pushVertex(float x, float y, float u, float v, const Color& color)
{
   Vertex2D vtx;
   vtx.x = x;
   vtx.y = y;
   vtx.u = u;
   vtx.v = v;
   vtx.color = color;
   mVertices.push_back(vtx);
}

void WebGL2Renderer::transformPoint(float& x, float& y)
{
   const float* t = mCurrentState.transform;
   const float nx = t[0] * x + t[2] * y + t[4];
   const float ny = t[1] * x + t[3] * y + t[5];
   x = nx;
   y = ny;
}

void WebGL2Renderer::endFrame()
{
   flushBatch();

   if (mVertices.empty())
   {
      mContext.endFrame();
      return;
   }

   glBindVertexArray(mVertexArray);
   glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
   glBufferSubData(GL_ARRAY_BUFFER, 0, mVertices.size() * sizeof(Vertex2D), mVertices.data());

   const float viewSize[2] = { static_cast<float>(mWidth), static_cast<float>(mHeight) };

   glDisable(GL_SCISSOR_TEST); // default for the frame
   for (const DrawCall& dc : mDrawCalls)
   {
      if (dc.vertexCount == 0)
         continue;

      if (dc.hasScissor)
      {
         glEnable(GL_SCISSOR_TEST);
         GLint sx = static_cast<GLint>(dc.scissor[0]);
         GLint sy = mHeight - static_cast<GLint>(dc.scissor[1] + dc.scissor[3]);
         GLint sw = static_cast<GLint>(dc.scissor[2]);
         GLint sh = static_cast<GLint>(dc.scissor[3]);
         if (sw < 0) sw = 0;
         if (sh < 0) sh = 0;
         glScissor(sx, sy, sw, sh);
      }
      else
      {
         glDisable(GL_SCISSOR_TEST);
      }

      const GL2Program& prog = programFor(dc.pipeline);
      if (!prog.program)
         continue;

      glUseProgram(prog.program);
      if (prog.uViewSize >= 0)
         glUniform2fv(prog.uViewSize, 1, viewSize);
      if (prog.uTime >= 0)
         glUniform1f(prog.uTime, mTime);

      glDrawArrays(GL_TRIANGLES, dc.firstVertex, dc.vertexCount);
   }

   glDisable(GL_SCISSOR_TEST);
   glBindVertexArray(0);
   mContext.endFrame();
}

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
   const float dAngle = da / numSegments;

   if (!mPathHasStart)
   {
      moveTo(cx + cosf(a0) * r, cy + sinf(a0) * r);
   }

   for (int i = 1; i <= numSegments; ++i)
   {
      const float angle = a0 + dAngle * i;
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
   cx /= numPoints;
   cy /= numPoints;

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
   return pixelFontTextWidth(string, mFontSize);
}

float WebGL2Renderer::textHeight() const
{
   return pixelFontTextHeight(mFontSize);
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

// Factory (declared in Renderer2D.h)
std::unique_ptr<Renderer2D> createWebGL2Renderer(WebGL2Context& context)
{
   return std::make_unique<WebGL2Renderer>(context);
}

} // namespace wasm
} // namespace bespoke
