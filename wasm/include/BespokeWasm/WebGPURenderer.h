/**
 * BespokeSynth WASM - WebGPU Renderer
 * Provides 2D rendering primitives using WebGPU for UI elements
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "Renderer2D.h"
#include "PixelFont.h"
#include "WebGPUContext.h"
#include <vector>
#include <string>
#include <cstdint>
#include <map>

namespace bespoke
{
   namespace wasm
   {

      // Shader Pipelines storage
      struct Pipelines
      {
         WGPURenderPipeline solid;
         WGPURenderPipeline textured;
         WGPURenderPipeline knob_highlight;
         WGPURenderPipeline wire_glow;
         WGPURenderPipeline vu_meter;
         WGPURenderPipeline connection_pulse;
         WGPURenderPipeline slider_track;
         WGPURenderPipeline slider_fill;
         WGPURenderPipeline slider_handle;
         WGPURenderPipeline button;
         WGPURenderPipeline button_hover;
         WGPURenderPipeline toggle_switch;
         WGPURenderPipeline toggle_thumb;
         WGPURenderPipeline adsr_envelope;
         WGPURenderPipeline adsr_grid;
         WGPURenderPipeline waveform;
         WGPURenderPipeline waveform_filled;
         WGPURenderPipeline spectrum_bar;
         WGPURenderPipeline spectrum_peak;
         WGPURenderPipeline panel_background;
         WGPURenderPipeline panel_bordered;
         WGPURenderPipeline text_glow;
         WGPURenderPipeline text_shadow;
         WGPURenderPipeline progress_bar;
         WGPURenderPipeline scope_display;
         WGPURenderPipeline scope_grid;
         WGPURenderPipeline led_indicator;
         WGPURenderPipeline led_off;
         WGPURenderPipeline dial_ticks;
         WGPURenderPipeline fader_groove;
         WGPURenderPipeline fader_cap;
         WGPURenderPipeline mod_wheel;
         WGPURenderPipeline pixel_text;
      };

   /**
 * WebGPU-based 2D renderer
 * Provides NanoVG-like API for drawing UI elements
 */
   class WebGPURenderer : public Renderer2D
   {
   public:
      WebGPURenderer(WebGPUContext& context);
      ~WebGPURenderer() override;

      RendererBackendType getBackendType() const override { return RendererBackendType::WebGPU; }

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
      float textHeight() const override { return pixelFontTextHeight(mFontSize); }

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

   private:
      // Per-frame draw call record: all vertices are uploaded once at endFrame()
      struct DrawCall
      {
         WGPURenderPipeline pipeline;
         uint32_t firstVertex;
         uint32_t vertexCount;
      };

      void createPipelines();
      void createBuffers();
      void flushBatch();
      void setPipeline(WGPURenderPipeline pipeline);
      void pushVertex(float x, float y, float u, float v, const Color& color);
      void transformPoint(float& x, float& y);
      void drawQuad(float x, float y, float w, float h, WGPURenderPipeline pipeline);

      WebGPUContext& mContext;

      Pipelines mPipelines;
      WGPURenderPipeline mCurrentPipeline = nullptr;
      WGPURenderPipeline mStrokePipeline = nullptr; // Kept separate for lines

      WGPUBuffer mVertexBuffer = nullptr;
      WGPUBuffer mUniformBuffer = nullptr;
      WGPUBindGroup mBindGroup = nullptr;
      WGPUBindGroupLayout mBindGroupLayout = nullptr; // Cached layout used even when pipelines are null
      WGPURenderPassEncoder mCurrentPass = nullptr;

      // All vertices accumulated for the entire frame; uploaded once at endFrame()
      std::vector<Vertex2D> mVertices;
      // Recorded draw calls; each references a contiguous range of mVertices
      std::vector<DrawCall> mDrawCalls;
      // Index of the first vertex in mVertices belonging to the current (not yet recorded) batch
      uint32_t mCurrentBatchFirstVertex = 0;

      // State stack
      struct State
      {
         float transform[6]; // 2D affine transform matrix
         Color fillColor;
         Color strokeColor;
         float strokeWidth;
         float scissor[4]; // x, y, w, h
         bool hasScissor;
      };
      std::vector<State> mStateStack;
      State mCurrentState;

      // Path building
      std::vector<float> mPathPoints;
      float mPathStartX, mPathStartY;
      float mPathX, mPathY;
      bool mPathHasStart;

      // Font state
      float mFontSize = 14.0f;
      std::string mFontName;

      int mWidth = 0;
      int mHeight = 0;
      float mPixelRatio = 1.0f;
      float mTime = 0.0f;
      bool mFrameStarted = false;
   };

} // namespace wasm
} // namespace bespoke
