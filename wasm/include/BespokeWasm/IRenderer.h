/**
 * BespokeSynth WASM - Renderer Abstraction Interface
 *
 * Defines the common drawing API shared by WebGPURenderer and WebGL2Renderer.
 * Consumers of the rendering API should depend on this interface so that the
 * backend can be swapped at initialization time without changing any
 * module-level drawing code.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstdint>
#include <string>

namespace bespoke
{
   namespace wasm
   {

      // -----------------------------------------------------------------------
      // Color — the canonical color type used by all renderers
      // -----------------------------------------------------------------------
      struct Color
      {
         float r, g, b, a;

         Color()
         : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}

         Color(float r, float g, float b, float a = 1.0f)
         : r(r), g(g), b(b), a(a) {}

         static Color fromRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
         {
            return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
         }
      };

      // -----------------------------------------------------------------------
      // RendererBackend
      // -----------------------------------------------------------------------

      enum class RendererBackend
      {
         Auto = 0,   ///< Prefer WebGPU; fall back to WebGL2 if unavailable
         WebGPU = 1, ///< Force WebGPU backend
         WebGL2 = 2, ///< Force WebGL2 backend
      };

      // -----------------------------------------------------------------------
      // IRenderer — pure-virtual 2D drawing API
      // -----------------------------------------------------------------------

      /**
       * Backend-agnostic 2D renderer interface.
       *
       * Both WebGPURenderer and WebGL2Renderer implement this interface so that
       * all module-level drawing code can be written against IRenderer and work
       * with either backend.
       */
      class IRenderer
      {
      public:
         virtual ~IRenderer() = default;

         // ------------------------------------------------------------------
         // Lifecycle
         // ------------------------------------------------------------------
         virtual bool initialize() = 0;

         /** Called at the start of each frame.  width/height are canvas pixels. */
         virtual void beginFrame(int width, int height, float pixelRatio, float time = 0.0f) = 0;

         /** Called at the end of each frame to flush and present. */
         virtual void endFrame() = 0;

         // ------------------------------------------------------------------
         // State stack
         // ------------------------------------------------------------------
         virtual void save() = 0;
         virtual void restore() = 0;
         virtual void reset() = 0;

         // ------------------------------------------------------------------
         // Transform
         // ------------------------------------------------------------------
         virtual void translate(float x, float y) = 0;
         virtual void rotate(float angle) = 0;
         virtual void scale(float x, float y) = 0;
         virtual void resetTransform() = 0;

         // ------------------------------------------------------------------
         // Scissor / clip
         // ------------------------------------------------------------------
         virtual void scissor(float x, float y, float w, float h) = 0;
         virtual void resetScissor() = 0;

         // ------------------------------------------------------------------
         // Style
         // ------------------------------------------------------------------
         virtual void fillColor(const Color& color) = 0;
         virtual void strokeColor(const Color& color) = 0;
         virtual void strokeWidth(float width) = 0;

         // ------------------------------------------------------------------
         // Path
         // ------------------------------------------------------------------
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

         // ------------------------------------------------------------------
         // Primitive shapes
         // ------------------------------------------------------------------
         virtual void rect(float x, float y, float w, float h) = 0;
         virtual void roundedRect(float x, float y, float w, float h, float r) = 0;
         virtual void circle(float cx, float cy, float r) = 0;
         virtual void ellipse(float cx, float cy, float rx, float ry) = 0;
         virtual void line(float x1, float y1, float x2, float y2) = 0;

         // ------------------------------------------------------------------
         // Text
         // ------------------------------------------------------------------
         virtual void fontSize(float size) = 0;
         virtual void fontFace(const char* name) = 0;
         virtual void text(float x, float y, const char* string) = 0;
         virtual float textWidth(const char* string) = 0;

         // ------------------------------------------------------------------
         // Specialised synth UI elements
         // ------------------------------------------------------------------
         virtual void drawKnob(float cx, float cy, float radius, float value,
                               const Color& bgColor, const Color& fgColor) = 0;
         virtual void drawWire(float x1, float y1, float x2, float y2,
                               const Color& color, float thickness = 2.0f) = 0;
         virtual void drawCableWithSag(float x1, float y1, float x2, float y2,
                                       const Color& color, float thickness = 3.0f,
                                       float sag = 0.3f) = 0;
         virtual void drawSlider(float x, float y, float w, float h, float value,
                                 const Color& bgColor, const Color& fgColor) = 0;
         virtual void drawVUMeter(float x, float y, float w, float h, float level,
                                  const Color& lowColor, const Color& highColor) = 0;
         virtual void drawButton(float x, float y, float w, float h, const char* label,
                                 bool pressed, bool hover) = 0;
         virtual void drawToggle(float x, float y, float w, float h, bool state) = 0;
         virtual void drawFader(float x, float y, float w, float h, float value) = 0;
         virtual void drawADSR(float x, float y, float w, float h,
                               float a, float d, float s, float r) = 0;
         virtual void drawPanel(float x, float y, float w, float h, bool bordered) = 0;
         virtual void drawLED(float x, float y, float w, float h, bool on) = 0;
         virtual void drawProgressBar(float x, float y, float w, float h, float value) = 0;

         // ------------------------------------------------------------------
         // Backend identification
         // ------------------------------------------------------------------
         virtual RendererBackend getBackend() const = 0;

         /**
          * Capture the current frame as RGBA8 pixels.
          * @param outWidth  Receives the canvas width in pixels.
          * @param outHeight Receives the canvas height in pixels.
          * @returns Heap-allocated RGBA8 pixel buffer (width * height * 4 bytes).
          *          Caller owns the memory; free with delete[].
          *          Returns nullptr if capture is not supported or fails.
          */
         virtual uint8_t* captureFrame(int& outWidth, int& outHeight) = 0;
      };

   } // namespace wasm
} // namespace bespoke
