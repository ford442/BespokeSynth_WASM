// CPU reference Renderer2D for widget tests: records every primitive call instead of drawing.
#pragma once

#include "BespokeWasm/Renderer2D.h"
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class RecordingRenderer : public Renderer2D
      {
      public:
         struct Call
         {
            std::string op;
            ShaderKind kind = ShaderKind::Solid; // "quad" only
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            Color color; // fill color for quad/fill/text, stroke color for stroke/line
            std::string text;
         };

         std::vector<Call> calls;
         WebGLDebugMode debugMode = WebGLDebugMode::Normal;

         size_t count(const std::string& op) const
         {
            size_t n = 0;
            for (const Call& call : calls)
               n += call.op == op ? 1 : 0;
            return n;
         }
         size_t countQuads(ShaderKind kind) const
         {
            size_t n = 0;
            for (const Call& call : calls)
               n += call.op == "quad" && call.kind == kind ? 1 : 0;
            return n;
         }
         const Call* firstQuad(ShaderKind kind) const
         {
            for (const Call& call : calls)
               if (call.op == "quad" && call.kind == kind)
                  return &call;
            return nullptr;
         }

         RendererBackendType getBackendType() const override { return RendererBackendType::WebGL2; }
         bool initialize() override { return true; }
         void beginFrame(int width, int height, float pixelRatio, float time) override
         {
            (void)width;
            (void)height;
            (void)pixelRatio;
            (void)time;
            calls.clear();
         }
         void endFrame() override {}

         void save() override
         {
            calls.push_back({ "save" });
            mStack.push_back(mState);
         }
         void restore() override
         {
            calls.push_back({ "restore" });
            if (!mStack.empty())
            {
               mState = mStack.back();
               mStack.pop_back();
            }
         }
         void reset() override { mState = {}; }

         void translate(float x, float y) override { calls.push_back({ "translate", ShaderKind::Solid, x, y }); }
         void rotate(float angle) override { calls.push_back({ "rotate", ShaderKind::Solid, angle }); }
         void scale(float x, float y) override { calls.push_back({ "scale", ShaderKind::Solid, x, y }); }
         void resetTransform() override { calls.push_back({ "resetTransform" }); }

         void scissor(float x, float y, float w, float h) override
         {
            calls.push_back({ "scissor", ShaderKind::Solid, x, y, w, h });
         }
         void resetScissor() override { calls.push_back({ "resetScissor" }); }

         void fillColor(const Color& color) override { mState.fill = color; }
         void strokeColor(const Color& color) override { mState.stroke = color; }
         void strokeWidth(float width) override { mState.strokeWidth = width; }

         void beginPath() override { calls.push_back({ "beginPath" }); }
         void moveTo(float x, float y) override { calls.push_back({ "moveTo", ShaderKind::Solid, x, y }); }
         void lineTo(float x, float y) override { calls.push_back({ "lineTo", ShaderKind::Solid, x, y }); }
         void bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override
         {
            (void)c1x;
            (void)c1y;
            (void)c2x;
            (void)c2y;
            calls.push_back({ "bezierTo", ShaderKind::Solid, x, y });
         }
         void quadTo(float cx, float cy, float x, float y) override
         {
            (void)cx;
            (void)cy;
            calls.push_back({ "quadTo", ShaderKind::Solid, x, y });
         }
         void arc(float cx, float cy, float r, float a0, float a1, int dir) override
         {
            (void)a0;
            (void)a1;
            (void)dir;
            calls.push_back({ "arc", ShaderKind::Solid, cx, cy, r, r });
         }
         void arcTo(float x1, float y1, float x2, float y2, float radius) override
         {
            (void)x1;
            (void)y1;
            calls.push_back({ "arcTo", ShaderKind::Solid, x2, y2, radius, radius });
         }
         void closePath() override { calls.push_back({ "closePath" }); }

         void fill() override { calls.push_back({ "fill", ShaderKind::Solid, 0, 0, 0, 0, mState.fill }); }
         void stroke() override { calls.push_back({ "stroke", ShaderKind::Solid, 0, 0, 0, 0, mState.stroke }); }

         void rect(float x, float y, float w, float h) override
         {
            calls.push_back({ "rect", ShaderKind::Solid, x, y, w, h });
         }
         void roundedRect(float x, float y, float w, float h, float r) override
         {
            (void)r;
            calls.push_back({ "roundedRect", ShaderKind::Solid, x, y, w, h });
         }
         void circle(float cx, float cy, float r) override
         {
            calls.push_back({ "circle", ShaderKind::Solid, cx, cy, r, r });
         }
         void ellipse(float cx, float cy, float rx, float ry) override
         {
            calls.push_back({ "ellipse", ShaderKind::Solid, cx, cy, rx, ry });
         }
         void line(float x1, float y1, float x2, float y2) override
         {
            calls.push_back({ "line", ShaderKind::Solid, x1, y1, x2 - x1, y2 - y1, mState.stroke });
         }

         void fontSize(float size) override { mState.fontSize = size; }
         void fontFace(const char* name) override { (void)name; }
         void text(float x, float y, const char* string) override
         {
            calls.push_back({ "text", ShaderKind::Solid, x, y, mState.fontSize, 0, mState.fill, string ? string : "" });
         }
         float textWidth(const char* string) override
         {
            return string ? static_cast<float>(std::char_traits<char>::length(string)) * mState.fontSize * 0.5f : 0.0f;
         }
         float textHeight() const override { return mState.fontSize; }

         void drawQuad(ShaderKind kind, float x, float y, float w, float h) override
         {
            calls.push_back({ "quad", kind, x, y, w, h, mState.fill });
         }
         Color currentFillColor() const override { return mState.fill; }

         void setDebugMode(WebGLDebugMode mode) override { debugMode = mode; }
         WebGLDebugMode getDebugMode() const override { return debugMode; }

      private:
         struct State
         {
            Color fill;
            Color stroke;
            float strokeWidth = 1.0f;
            float fontSize = 12.0f;
         };
         State mState;
         std::vector<State> mStack;
      };

   } // namespace wasm
} // namespace bespoke
