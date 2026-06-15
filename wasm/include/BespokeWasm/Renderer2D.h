/**
 * BespokeSynth WASM - 2D Renderer abstraction
 * Shared interface for WebGPU and WebGL2 backends
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Forward declarations for context types used by the renderer factories.
// These classes are intentionally defined in the *global* namespace (their headers
// do not wrap them in bespoke::wasm). Forwarding here lets us expose the tiny
// creation helpers from this header without WasmBridge / call sites needing to
// #include the concrete renderer headers (WebGPURenderer.h, WebGL2Renderer.h).
class WebGPUContext;
class WebGL2Context;

namespace bespoke {
namespace wasm {

struct Color
{
   float r, g, b, a;

   Color()
   : r(1.0f)
   , g(1.0f)
   , b(1.0f)
   , a(1.0f)
   {
   }
   Color(float r, float g, float b, float a = 1.0f)
   : r(r)
   , g(g)
   , b(b)
   , a(a)
   {
   }

   static Color fromRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
   {
      return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
   }
};

struct Vertex2D
{
   float x, y;
   float u, v;
   Color color;
};

enum class RendererBackendType
{
   WebGPU = 0,
   WebGL2 = 1
};

enum class WebGLDebugMode
{
   Normal = 0,
   Wireframe = 1,
   ConnectionDebug = 2,
   SimplifiedModules = 3
};

/**
 * NanoVG-like immediate-mode 2D renderer interface.
 * ModuleCanvas, Knob, and demo panels render through this API.
 */
class Renderer2D
{
public:
   virtual ~Renderer2D() = default;

   virtual RendererBackendType getBackendType() const = 0;

   virtual bool initialize() = 0;

   virtual void beginFrame(int width, int height, float pixelRatio, float time = 0.0f) = 0;
   virtual void endFrame() = 0;

   virtual void save() = 0;
   virtual void restore() = 0;
   virtual void reset() = 0;

   virtual void translate(float x, float y) = 0;
   virtual void rotate(float angle) = 0;
   virtual void scale(float x, float y) = 0;
   virtual void resetTransform() = 0;

   virtual void scissor(float x, float y, float w, float h) = 0;
   virtual void resetScissor() = 0;

   virtual void fillColor(const Color& color) = 0;
   virtual void strokeColor(const Color& color) = 0;
   virtual void strokeWidth(float width) = 0;

   virtual void beginPath() = 0;
   virtual void moveTo(float x, float y) = 0;
   virtual void lineTo(float x, float y) = 0;
   virtual void bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y) = 0;
   virtual void quadTo(float cx, float cy, float x, float y) = 0;
   virtual void arc(float cx, float cy, float r, float a0, float a1, int dir) = 0;
   virtual void arcTo(float x1, float y1, float x2, float y2, float radius) = 0;
   virtual void closePath() = 0;

   virtual void fill() = 0;
   virtual void stroke() = 0;

   virtual void rect(float x, float y, float w, float h) = 0;
   virtual void roundedRect(float x, float y, float w, float h, float r) = 0;
   virtual void circle(float cx, float cy, float r) = 0;
   virtual void ellipse(float cx, float cy, float rx, float ry) = 0;
   virtual void line(float x1, float y1, float x2, float y2) = 0;

   virtual void fontSize(float size) = 0;
   virtual void fontFace(const char* name) = 0;
   virtual void text(float x, float y, const char* string) = 0;
   virtual float textWidth(const char* string) = 0;
   virtual float textHeight() const = 0;

   virtual void drawKnob(float cx, float cy, float radius, float value, const Color& bgColor, const Color& fgColor) = 0;
   virtual void drawWire(float x1, float y1, float x2, float y2, const Color& color, float thickness = 2.0f) = 0;
   virtual void drawCableWithSag(float x1, float y1, float x2, float y2, const Color& color, float thickness = 3.0f, float sag = 0.3f) = 0;
   virtual void drawSlider(float x, float y, float w, float h, float value, const Color& bgColor, const Color& fgColor) = 0;
   virtual void drawVUMeter(float x, float y, float w, float h, float level, const Color& lowColor, const Color& highColor) = 0;

   virtual void drawButton(float x, float y, float w, float h, const char* label, bool pressed, bool hover) = 0;
   virtual void drawToggle(float x, float y, float w, float h, bool state) = 0;
   virtual void drawFader(float x, float y, float w, float h, float value) = 0;
   virtual void drawModWheel(float x, float y, float w, float h, float value) = 0;
   virtual void drawADSR(float x, float y, float w, float h, float a, float d, float s, float r) = 0;
   virtual void drawWaveform(float x, float y, float w, float h, const float* data, int count, bool filled) = 0;
   virtual void drawSpectrum(float x, float y, float w, float h, const float* data, int count) = 0;
   virtual void drawScope(float x, float y, float w, float h, const float* data, int count) = 0;
   virtual void drawPanel(float x, float y, float w, float h, bool bordered) = 0;
   virtual void drawLED(float x, float y, float w, float h, bool on) = 0;
   virtual void drawProgressBar(float x, float y, float w, float h, float value) = 0;

   virtual void drawXYPad(float x, float y, float w, float h, float cx, float cy) = 0;
   virtual void drawFilterResponse(float x, float y, float w, float h) = 0;
   virtual void drawLFOWaveform(float x, float y, float w, float h) = 0;
   virtual void drawSequencerStep(float x, float y, float w, float h, bool active) = 0;
   virtual void drawSpectrumWaterfall(float x, float y, float w, float h) = 0;
   virtual void drawPianoKey(float x, float y, float w, float h, bool black, bool pressed) = 0;
   virtual void drawSpectrumRainbow(float x, float y, float w, float h, float* data, int count) = 0;
   virtual void drawCircularScope(float x, float y, float w, float h) = 0;
   virtual void drawEchoTrail(float x, float y, float w, float h) = 0;

   virtual void setDebugMode(WebGLDebugMode mode) { (void)mode; }
   virtual WebGLDebugMode getDebugMode() const { return WebGLDebugMode::Normal; }
};

// Small factories (declared inside the namespace for the return/usage style of the rest of the
// wasm code, but the parameter types refer to the global-namespace forwards above).
std::unique_ptr<Renderer2D> createWebGPURenderer(WebGPUContext& context);
std::unique_ptr<Renderer2D> createWebGL2Renderer(WebGL2Context& context);

} // namespace wasm
} // namespace bespoke
