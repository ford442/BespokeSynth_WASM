/**
 * BespokeSynth WASM - WebGL2 Renderer Implementation
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "WebGL2Renderer.h"
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

static const char* kVertexShaderSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec4 aColor;
uniform vec2 uViewSize;
out vec2 vTexcoord;
out vec4 vColor;
void main() {
    float clipX = (aPosition.x / uViewSize.x) * 2.0 - 1.0;
    float clipY = 1.0 - (aPosition.y / uViewSize.y) * 2.0;
    gl_Position = vec4(clipX, clipY, 0.0, 1.0);
    vTexcoord = aTexcoord;
    vColor = aColor;
}
)";

static const char* kSolidFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

static const char* kWireGlowFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float dist = abs(vTexcoord.y - 0.5) * 2.0;
    float core = smoothstep(0.3, 0.0, dist);
    float glow = smoothstep(1.0, 0.0, dist) * 0.5;
    vec4 color = vColor;
    color.a *= core + glow;
    fragColor = color;
}
)";

static const char* kKnobHighlightFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(vTexcoord, center);
    vec2 lightDir = normalize(vec2(-0.5, -0.5));
    vec2 normal = normalize(vTexcoord - center);
    float highlight = max(0.0, dot(normal, lightDir));
    vec4 color = vColor;
    color.rgb += highlight * 0.3;
    float edgeDark = smoothstep(0.3, 0.5, dist);
    color.rgb *= 1.0 - edgeDark * 0.3;
    fragColor = color;
}
)";

// Pixel font fragment shader - glyph index encoded in texcoord.x
static const char* kPixelTextFragmentPrefix = R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
const int FONT_GLYPHS = )";

static const char* kPixelTextFragmentSuffix = R"(;
uniform int uFontCols[475];
void main() {
    int charIdx = clamp(int(floor(vTexcoord.x)), 0, FONT_GLYPHS - 1);
    float localX = fract(vTexcoord.x);
    int px = clamp(int(localX * 5.0), 0, 4);
    int py = clamp(int(vTexcoord.y * 7.0), 0, 6);
    int colData = uFontCols[charIdx * 5 + px];
    int pixelOn = (colData >> py) & 1;
    if (pixelOn == 0) {
        discard;
    }
    fragColor = vColor;
}
)";

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
   if (mSolidProgram)
      glDeleteProgram(mSolidProgram);
   if (mPixelTextProgram)
      glDeleteProgram(mPixelTextProgram);
   if (mWireGlowProgram)
      glDeleteProgram(mWireGlowProgram);
   if (mKnobHighlightProgram)
      glDeleteProgram(mKnobHighlightProgram);
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

void WebGL2Renderer::createPrograms()
{
   GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShaderSrc);

   GLuint fsSolid = compileShader(GL_FRAGMENT_SHADER, kSolidFragmentSrc);
   mSolidProgram = linkProgram(vs, fsSolid);
   mSolidViewSizeLoc = glGetUniformLocation(mSolidProgram, "uViewSize");
   glDeleteShader(fsSolid);

   GLuint fsWire = compileShader(GL_FRAGMENT_SHADER, kWireGlowFragmentSrc);
   mWireGlowProgram = linkProgram(vs, fsWire);
   mWireGlowViewSizeLoc = glGetUniformLocation(mWireGlowProgram, "uViewSize");
   glDeleteShader(fsWire);

   GLuint fsKnob = compileShader(GL_FRAGMENT_SHADER, kKnobHighlightFragmentSrc);
   mKnobHighlightProgram = linkProgram(vs, fsKnob);
   mKnobViewSizeLoc = glGetUniformLocation(mKnobHighlightProgram, "uViewSize");
   glDeleteShader(fsKnob);

   std::string pixelTextSrc = kPixelTextFragmentPrefix;
   pixelTextSrc += std::to_string(kPixelFontGlyphCount);
   pixelTextSrc += kPixelTextFragmentSuffix;
   GLuint fsText = compileShader(GL_FRAGMENT_SHADER, pixelTextSrc.c_str());
   mPixelTextProgram = linkProgram(vs, fsText);
   mPixelTextViewSizeLoc = glGetUniformLocation(mPixelTextProgram, "uViewSize");

   const GLint fontColsLoc = glGetUniformLocation(mPixelTextProgram, "uFontCols");
   if (fontColsLoc >= 0)
   {
      const uint32_t* cols = getPixelFontColumns();
      std::vector<GLint> fontData(kPixelFontGlyphCount * kPixelFontColumnsPerGlyph);
      for (size_t i = 0; i < fontData.size(); ++i)
         fontData[i] = static_cast<GLint>(cols[i]);
      glUseProgram(mPixelTextProgram);
      glUniform1iv(fontColsLoc, static_cast<GLsizei>(fontData.size()), fontData.data());
   }

   glDeleteShader(vs);
   glDeleteShader(fsText);
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
   return mSolidProgram != 0 && mPixelTextProgram != 0;
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

   for (const DrawCall& dc : mDrawCalls)
   {
      if (dc.vertexCount == 0)
         continue;

      GLuint program = mSolidProgram;
      GLint viewLoc = mSolidViewSizeLoc;
      switch (dc.pipeline)
      {
         case GLPipelineKind::Solid:
            program = mSolidProgram;
            viewLoc = mSolidViewSizeLoc;
            break;
         case GLPipelineKind::PixelText:
            program = mPixelTextProgram;
            viewLoc = mPixelTextViewSizeLoc;
            break;
         case GLPipelineKind::WireGlow:
            program = mWireGlowProgram;
            viewLoc = mWireGlowViewSizeLoc;
            break;
         case GLPipelineKind::KnobHighlight:
            program = mKnobHighlightProgram;
            viewLoc = mKnobViewSizeLoc;
            break;
      }

      glUseProgram(program);
      glUniform2fv(viewLoc, 1, viewSize);
      glDrawArrays(GL_TRIANGLES, dc.firstVertex, dc.vertexCount);
   }

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
   mCurrentState.scissor[0] = x;
   mCurrentState.scissor[1] = y;
   mCurrentState.scissor[2] = w;
   mCurrentState.scissor[3] = h;
   mCurrentState.hasScissor = true;
}

void WebGL2Renderer::resetScissor()
{
   mCurrentState.hasScissor = false;
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

      if (mCurrentState.strokeWidth <= 1.5f || mDebugMode == WebGLDebugMode::Wireframe)
      {
         pushVertex(x1, y1, 0.0f, 0.0f, mCurrentState.strokeColor);
         pushVertex(x2, y2, 0.0f, 0.0f, mCurrentState.strokeColor);
         continue;
      }

      const float nx = -dy / len * mCurrentState.strokeWidth * 0.5f;
      const float ny = dx / len * mCurrentState.strokeWidth * 0.5f;
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

   const float charWidth = mFontSize * kPixelFontCharWidthRatio;
   const float charHeight = mFontSize;
   const float charSpacing = mFontSize * kPixelFontCharSpacingRatio;
   const Color textColor = mCurrentState.fillColor;

   setPipeline(GLPipelineKind::PixelText);

   float currentX = x;
   const size_t len = strlen(string);
   for (size_t i = 0; i < len; ++i)
   {
      const unsigned char c = static_cast<unsigned char>(string[i]);
      const int charIdx = pixelFontCharIndex(c);
      if (c == ' ')
      {
         currentX += charWidth + charSpacing;
         continue;
      }

      const float u0 = static_cast<float>(charIdx);
      const float u1 = static_cast<float>(charIdx) + 1.0f;
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
   circle(cx, cy, radius * 0.85f);
   fill();

   const float startA = 0.75f * PI;
   const float valA = startA + value * 1.5f * PI;
   strokeColor(UITheme::kAccentCyan);
   strokeWidth(2.0f);
   beginPath();
   moveTo(cx, cy);
   lineTo(cx + cosf(valA) * radius * 0.7f, cy + sinf(valA) * radius * 0.7f);
   stroke();
}

void WebGL2Renderer::drawWire(float x1, float y1, float x2, float y2, const Color& color, float thickness)
{
   fillColor(color);
   const float dx = x2 - x1;
   const float dy = y2 - y1;
   const float len = sqrtf(dx * dx + dy * dy);
   if (len < 0.0001f)
      return;
   const float nx = -dy / len * thickness * 0.5f;
   const float ny = dx / len * thickness * 0.5f;
   drawQuad(x1 - nx, y1 - ny, len, thickness, GLPipelineKind::WireGlow);
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
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
   fillColor(fgColor);
   if (h > w)
   {
      const float fillH = h * value;
      drawQuad(x, y + h - fillH, w, fillH, GLPipelineKind::Solid);
   }
   else
   {
      const float fillW = w * value;
      drawQuad(x, y, fillW, h, GLPipelineKind::Solid);
   }
}

void WebGL2Renderer::drawVUMeter(float x, float y, float w, float h, float level, const Color& lowColor, const Color& highColor)
{
   fillColor(Color(0.1f, 0.1f, 0.12f, 1.0f));
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
   const float fillH = h * std::clamp(level, 0.0f, 1.0f);
   fillColor(level > 0.85f ? highColor : lowColor);
   drawQuad(x, y + h - fillH, w, fillH, GLPipelineKind::Solid);
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
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
   if (hover)
   {
      fillColor(Color(1.0f, 1.0f, 1.0f, 0.2f));
      drawQuad(x, y, w, h, GLPipelineKind::Solid);
   }
   if (label && label[0])
   {
      fillColor(UITheme::kTextPrimary);
      fontSize(11.0f);
      const float labelW = textWidth(label);
      text(x + (w - labelW) * 0.5f, y + h * 0.65f, label);
   }
}

void WebGL2Renderer::drawToggle(float x, float y, float w, float h, bool state)
{
   fillColor(Color(0.2f, 0.2f, 0.22f, 1.0f));
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
   fillColor(state ? UITheme::kAccentGreen : Color(0.35f, 0.35f, 0.38f, 1.0f));
   const float pad = 2.0f;
   const float thumbW = (w - pad * 3.0f) * 0.5f;
   const float thumbX = state ? x + w - pad - thumbW : x + pad;
   drawQuad(thumbX, y + pad, thumbW, h - pad * 2.0f, GLPipelineKind::Solid);
}

void WebGL2Renderer::drawFader(float x, float y, float w, float h, float value)
{
   drawSlider(x, y, w, h, value, Color(0.15f, 0.15f, 0.17f, 1.0f), UITheme::kAccentCyan);
}

void WebGL2Renderer::drawModWheel(float x, float y, float w, float h, float value)
{
   drawSlider(x, y, w, h, value, Color(0.12f, 0.12f, 0.14f, 1.0f), UITheme::kAccentMagenta);
}

void WebGL2Renderer::drawADSR(float x, float y, float w, float h, float a, float d, float s, float r)
{
   fillColor(Color(0.1f, 0.1f, 0.12f, 1.0f));
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
   strokeColor(UITheme::kAccentCyan);
   strokeWidth(2.0f);
   beginPath();
   moveTo(x, y + h);
   lineTo(x + w * a * 0.25f, y);
   lineTo(x + w * (a * 0.25f + d * 0.25f), y + h * (1.0f - s));
   lineTo(x + w * 0.75f, y + h * (1.0f - s));
   lineTo(x + w, y + h);
   stroke();
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
      fillColor(UITheme::kAccentGreen);
      drawQuad(x + i * barW, y + h - barH, barW - 1.0f, barH, GLPipelineKind::Solid);
   }
}

void WebGL2Renderer::drawScope(float x, float y, float w, float h, const float* data, int count)
{
   drawWaveform(x, y, w, h, data, count, false);
}

void WebGL2Renderer::drawPanel(float x, float y, float w, float h, bool bordered)
{
   fillColor(UITheme::kBgPanel);
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
   if (bordered)
   {
      strokeColor(Color(0.35f, 0.35f, 0.4f, 1.0f));
      strokeWidth(1.0f);
      rect(x, y, w, h);
      stroke();
   }
}

void WebGL2Renderer::drawLED(float x, float y, float w, float h, bool on)
{
   fillColor(on ? UITheme::kAccentGreen : Color(0.2f, 0.2f, 0.22f, 1.0f));
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
}

void WebGL2Renderer::drawProgressBar(float x, float y, float w, float h, float value)
{
   drawSlider(x, y, w, h, value, Color(0.15f, 0.15f, 0.17f, 1.0f), UITheme::kAccentAmber);
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
   drawQuad(x, y, w, h, GLPipelineKind::Solid);
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
