/**
 * BespokeSynth WASM - WebGL2 Renderer
 *
<<<<<<< HEAD
 * Implements the IRenderer drawing API using the browser's WebGL2 (OpenGL ES 3)
 * context through Emscripten's OpenGL bindings.  WebGL2 is supported by all
 * modern browsers without any special flags and permits straightforward canvas
 * screenshots via gl.readPixels().
 *
=======
>>>>>>> origin/main
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

<<<<<<< HEAD
#include "IRenderer.h"

#include <vector>
#include <string>
#include <cstdint>

// Emscripten OpenGL ES 3 / WebGL2 header
#ifdef __EMSCRIPTEN__
#  include <GLES3/gl3.h>
#  include <emscripten.h>
#endif

namespace bespoke
{
   namespace wasm
   {

      /**
       * WebGL2-backed 2D renderer.
       *
       * Provides the same drawing API as WebGPURenderer but targets WebGL2
       * instead of WebGPU.  This makes it compatible with a wider range of
       * browsers and easier to use with standard canvas capture tooling.
       *
       * The renderer uses a single vertex array / buffer per frame and batches
       * draw calls per shader program to minimise GL state changes.
       */
      class WebGL2Renderer : public IRenderer
      {
      public:
         WebGL2Renderer();
         ~WebGL2Renderer() override;

         // ------------------------------------------------------------------
         // IRenderer lifecycle
         // ------------------------------------------------------------------
         bool initialize() override;
         void beginFrame(int width, int height, float pixelRatio, float time = 0.0f) override;
         void endFrame() override;

         // ------------------------------------------------------------------
         // State
         // ------------------------------------------------------------------
         void save() override;
         void restore() override;
         void reset() override;

         // ------------------------------------------------------------------
         // Transform
         // ------------------------------------------------------------------
         void translate(float x, float y) override;
         void rotate(float angle) override;
         void scale(float x, float y) override;
         void resetTransform() override;

         // ------------------------------------------------------------------
         // Scissor
         // ------------------------------------------------------------------
         void scissor(float x, float y, float w, float h) override;
         void resetScissor() override;

         // ------------------------------------------------------------------
         // Style
         // ------------------------------------------------------------------
         void fillColor(const Color& color) override;
         void strokeColor(const Color& color) override;
         void strokeWidth(float width) override;

         // ------------------------------------------------------------------
         // Path
         // ------------------------------------------------------------------
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

         // ------------------------------------------------------------------
         // Shapes
         // ------------------------------------------------------------------
         void rect(float x, float y, float w, float h) override;
         void roundedRect(float x, float y, float w, float h, float r) override;
         void circle(float cx, float cy, float r) override;
         void ellipse(float cx, float cy, float rx, float ry) override;
         void line(float x1, float y1, float x2, float y2) override;

         // ------------------------------------------------------------------
         // Text
         // ------------------------------------------------------------------
         void fontSize(float size) override;
         void fontFace(const char* name) override;
         void text(float x, float y, const char* string) override;
         float textWidth(const char* string) override;

         // ------------------------------------------------------------------
         // Synth UI elements
         // ------------------------------------------------------------------
         void drawKnob(float cx, float cy, float radius, float value,
                       const Color& bgColor, const Color& fgColor) override;
         void drawWire(float x1, float y1, float x2, float y2,
                       const Color& color, float thickness = 2.0f) override;
         void drawCableWithSag(float x1, float y1, float x2, float y2,
                               const Color& color, float thickness = 3.0f,
                               float sag = 0.3f) override;
         void drawSlider(float x, float y, float w, float h, float value,
                         const Color& bgColor, const Color& fgColor) override;
         void drawVUMeter(float x, float y, float w, float h, float level,
                          const Color& lowColor, const Color& highColor) override;
         void drawButton(float x, float y, float w, float h, const char* label,
                         bool pressed, bool hover) override;
         void drawToggle(float x, float y, float w, float h, bool state) override;
         void drawFader(float x, float y, float w, float h, float value) override;
         void drawADSR(float x, float y, float w, float h,
                       float a, float d, float s, float r) override;
         void drawPanel(float x, float y, float w, float h, bool bordered) override;
         void drawLED(float x, float y, float w, float h, bool on) override;
         void drawProgressBar(float x, float y, float w, float h, float value) override;

         // ------------------------------------------------------------------
         // Backend identification
         // ------------------------------------------------------------------
         RendererBackend getBackend() const override { return RendererBackend::WebGL2; }

         // ------------------------------------------------------------------
         // Screenshot
         // ------------------------------------------------------------------
         uint8_t* captureFrame(int& outWidth, int& outHeight) override;

      private:
         // -- Internal vertex type ------------------------------------------
         struct GL2Vertex
         {
            float x, y;   // screen-space position (pixels)
            float u, v;   // texcoord (glyph encoding for text shader)
            float r, g, b, a; // per-vertex colour
         };

         // -- State -----------------------------------------------------------
         struct State
         {
            float transform[6]; // [sx, kx, ky, sy, tx, ty]
            Color fillColor;
            Color strokeColor;
            float strokeWidth;
         };
         std::vector<State> mStateStack;
         State mCurrentState;

         // -- Path ------------------------------------------------------------
         std::vector<float> mPathPoints; // interleaved x,y pairs
         float mPathStartX = 0.0f, mPathStartY = 0.0f;
         float mPathX = 0.0f, mPathY = 0.0f;
         bool  mPathHasStart = false;

         // -- Font ------------------------------------------------------------
         float       mFontSize = 14.0f;
         std::string mFontName;

         // -- Viewport --------------------------------------------------------
         int   mWidth = 0;
         int   mHeight = 0;
         float mPixelRatio = 1.0f;
         float mTime = 0.0f;

         // -- GL objects ------------------------------------------------------
#ifdef __EMSCRIPTEN__
         GLuint mVAO = 0;
         GLuint mVBO = 0;
         GLuint mProgSolid = 0;     // Basic solid-colour program
         GLuint mProgPixelText = 0; // 5×7 pixel-font program
         GLuint mProgKnob = 0;      // Knob highlight program
         GLuint mProgSlider = 0;    // Slider program
         GLuint mProgCable = 0;     // Cable/wire program
         GLuint mProgVU = 0;        // VU meter program

         // Font texture: 85 chars × 5 cols packed into a 1D R8 texture atlas
         GLuint mFontTexture = 0;
#endif

         std::vector<GL2Vertex> mVertices; // accumulated per frame

         // -- Helpers ---------------------------------------------------------
         void transformPoint(float& x, float& y) const;
         void pushQuad(float x, float y, float w, float h, const Color& c);
         void pushFilledPath(const Color& c);
         void pushStrokedPath(const Color& c, float width);
         void flushVertices(GLuint program);

         // Shader helpers
         static GLuint compileShader(GLenum type, const char* src);
         static GLuint linkProgram(GLuint vert, GLuint frag);
         void setUniformViewSize(GLuint prog) const;

         // Font texture helpers
         void buildFontTexture();
         void renderGlyph(float x, float y, int charIdx, float charW, float charH,
                          const Color& color);

         static const int kArcSegments = 16;
         static const float kPI;
      };

   } // namespace wasm
=======
#include "Renderer2D.h"
#include "WebGL2Context.h"
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

   void drawKnob(float cx, float cy, float radius, float value, const Color& bgColor, const Color& fgColor) override;
   void drawWire(float x1, float y1, float x2, float y2, const Color& color, float thickness = 2.0f) override;
   void drawCableWithSag(float x1, float y1, float x2, float y2, const Color& color, float thickness = 3.0f, float sag = 0.3f) override;
   void drawSlider(float x, float y, float w, float h, float value, const Color& bgColor, const Color& fgColor) override;
   void drawVUMeter(float x, float y, float w, float h, float level, const Color& lowColor, const Color& highColor) override;

   void drawButton(float x, float y, float w, float h, const char* label, bool pressed, bool hover) override;
   void drawToggle(float x, float y, float w, float h, bool state) override;
   void drawFader(float x, float y, float w, float h, float value) override;
   void drawModWheel(float x, float y, float w, float h, float value) override;
   void drawADSR(float x, float y, float w, float h, float a, float d, float s, float r) override;
   void drawWaveform(float x, float y, float w, float h, const float* data, int count, bool filled) override;
   void drawSpectrum(float x, float y, float w, float h, const float* data, int count) override;
   void drawScope(float x, float y, float w, float h, const float* data, int count) override;
   void drawPanel(float x, float y, float w, float h, bool bordered) override;
   void drawLED(float x, float y, float w, float h, bool on) override;
   void drawProgressBar(float x, float y, float w, float h, float value) override;

   void drawXYPad(float x, float y, float w, float h, float cx, float cy) override;
   void drawFilterResponse(float x, float y, float w, float h) override;
   void drawLFOWaveform(float x, float y, float w, float h) override;
   void drawSequencerStep(float x, float y, float w, float h, bool active) override;
   void drawSpectrumWaterfall(float x, float y, float w, float h) override;
   void drawPianoKey(float x, float y, float w, float h, bool black, bool pressed) override;
   void drawSpectrumRainbow(float x, float y, float w, float h, float* data, int count) override;
   void drawCircularScope(float x, float y, float w, float h) override;
   void drawEchoTrail(float x, float y, float w, float h) override;

   void setDebugMode(WebGLDebugMode mode) override { mDebugMode = mode; }
   WebGLDebugMode getDebugMode() const override { return mDebugMode; }

private:
   struct GL2Program
   {
      GLuint program = 0;
      GLint uViewSize = -1;
      GLint uTime = -1;
   };

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
>>>>>>> origin/main
} // namespace bespoke
