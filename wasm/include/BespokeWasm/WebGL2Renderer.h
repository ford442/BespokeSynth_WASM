/**
 * BespokeSynth WASM - WebGL2 Renderer
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/Renderer2D.h"
#include "BespokeWasm/WebGL2Context.h"
#include <GLES3/gl3.h>
#include <array>
#include <vector>
#include <string>

namespace bespoke {
namespace wasm {

enum class GLPipelineKind
{
   Solid,
   PixelText,
   SdfText,
   KnobHighlight,
   DialTicks,
   WireGlow,
   ConnectionPulse,
   SliderTrack,
   SliderFill,
   SliderHandle,
   Button,
   ButtonHover,
   ToggleSwitch,
   ToggleThumb,
   VUMeter,
   ADSRGrid,
   ADSREnvelope,
   PanelBackground,
   PanelBordered,
   LEDOn,
   LEDOff,
   Waveform,
   WaveformFilled,
   SpectrumBar,
   SpectrumPeak,
   ProgressBar,
   ModWheel,
   ScopeGrid,
   FaderGroove,
   FaderCap,
   Count
};

class WebGL2Renderer : public Renderer2D
{
public:
   explicit WebGL2Renderer(WebGL2Context& context);
   ~WebGL2Renderer() override;

   RendererBackendType getBackendType() const override { return RendererBackendType::WebGL2; }

   bool initialize() override;

   void beginFrame(int width, int height, float pixelRatio, float time = 0.0f) override;
   void endFrame() override;

   void save() override;
   void restore() override;
   void reset() override;

   void translate(float x, float y) override;
   void rotate(float angle) override;
   void scale(float x, float y) override;
   void resetTransform() override;

   void scissor(float x, float y, float w, float h) override;
   void resetScissor() override;

   void fillColor(const Color& color) override;
   void strokeColor(const Color& color) override;
   void strokeWidth(float width) override;

   void beginPath() override;
   void moveTo(float x, float y) override;
   void lineTo(float x, float y) override;
   void bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override;
   void quadTo(float cx, float cy, float x, float y) override;
   void arc(float cx, float cy, float r, float a0, float a1, int dir) override;
   void arcTo(float x1, float y1, float x2, float y2, float radius) override;
   void closePath() override;

   void fill() override;
   void stroke() override;

   void rect(float x, float y, float w, float h) override;
   void roundedRect(float x, float y, float w, float h, float r) override;
   void circle(float cx, float cy, float r) override;
   void ellipse(float cx, float cy, float rx, float ry) override;
   void line(float x1, float y1, float x2, float y2) override;

   void fontSize(float size) override;
   void fontFace(const char* name) override;
   void text(float x, float y, const char* string) override;
   float textWidth(const char* string) override;
   float textHeight() const override;

   void drawQuad(ShaderKind kind, float x, float y, float w, float h) override;
   Color currentFillColor() const override { return mCurrentState.fillColor; }

   void setDebugMode(WebGLDebugMode mode) override { mDebugMode = mode; }
   WebGLDebugMode getDebugMode() const override { return mDebugMode; }

private:
   struct GL2Program
   {
      GLuint program = 0;
      GLint uViewSize = -1;
      GLint uTime = -1;
   };

   bool sdfTextActive() const;
   void createTextAtlas();

   struct DrawCall
   {
      GLPipelineKind pipeline;
      uint32_t firstVertex;
      uint32_t vertexCount;
      bool hasScissor;
      float scissor[4];
   };

   struct State
   {
      float transform[6];
      Color fillColor;
      Color strokeColor;
      float strokeWidth;
      float scissor[4];
      bool hasScissor;
   };

   GLuint compileShader(GLenum type, const char* source);
   GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);
   bool buildProgram(GLPipelineKind kind, const char* fragmentSrc, bool needsTime);
   void createPrograms();
   void createBuffers();
   void flushBatch();
   void setPipeline(GLPipelineKind pipeline);
   void pushVertex(float x, float y, float u, float v, const Color& color);
   void transformPoint(float& x, float& y);
   void drawQuad(float x, float y, float w, float h, GLPipelineKind pipeline);
   void drawSolidQuad(float x, float y, float w, float h, const Color& color);
   const GL2Program& programFor(GLPipelineKind kind) const;

   WebGL2Context& mContext;

   GLuint mSharedVertexShader = 0;
   std::array<GL2Program, static_cast<size_t>(GLPipelineKind::Count)> mPrograms{};

   GLuint mVertexBuffer = 0;
   GLuint mVertexArray = 0;
   GLuint mAtlasTexture = 0; ///< SdfFont atlas (R8), bound to unit 0 for SdfText

   GLPipelineKind mCurrentPipeline = GLPipelineKind::Solid;

   std::vector<Vertex2D> mVertices;
   std::vector<DrawCall> mDrawCalls;
   uint32_t mCurrentBatchFirstVertex = 0;

   std::vector<State> mStateStack;
   State mCurrentState;

   std::vector<float> mPathPoints;
   float mPathStartX = 0.0f;
   float mPathStartY = 0.0f;
   float mPathX = 0.0f;
   float mPathY = 0.0f;
   bool mPathHasStart = false;

   float mFontSize = 14.0f;
   std::string mFontName;

   int mWidth = 0;
   int mHeight = 0;
   float mPixelRatio = 1.0f;
   float mTime = 0.0f;
   WebGLDebugMode mDebugMode = WebGLDebugMode::Normal;
};

} // namespace wasm
} // namespace bespoke
