/**
 * BespokeSynth WASM - WebGPU Renderer Implementation
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WebGPURenderer.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/PixelFont.h"
#include "BespokeWasm/SdfFont.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace bespoke {
namespace wasm {

// Constants for 2D rendering
static const int kArcTessellationFactor = 4;  // Arc subdivisions per radius unit
static const float PI = 3.14159265f;
static const float TWO_PI = 6.28318530f;
static const float HALF_PI = 1.57079632f;


WebGPURenderer::WebGPURenderer(WebGPUContext& context)
    : mContext(context)
{
    // Initialize default state
    mCurrentState.transform[0] = 1.0f;  // Scale X
    mCurrentState.transform[1] = 0.0f;  // Skew X
    mCurrentState.transform[2] = 0.0f;  // Skew Y
    mCurrentState.transform[3] = 1.0f;  // Scale Y
    mCurrentState.transform[4] = 0.0f;  // Translate X
    mCurrentState.transform[5] = 0.0f;  // Translate Y
    mCurrentState.fillColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
    mCurrentState.strokeColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
    mCurrentState.strokeWidth = 1.0f;
    mCurrentState.hasScissor = false;
}

void WebGPURenderer::beginFrame(int width, int height, float pixelRatio, float time) {
    mWidth = width;
    mHeight = height;
    mPixelRatio = pixelRatio;
    mTime = time;
    mFrameStarted = true;
    
    mVertices.clear();
    mDrawCalls.clear();
    mCurrentBatchFirstVertex = 0;
    mAnalyzerRequested = false;

    mContext.beginFrame();

    // Update uniform buffer with view size and time (atlas/analyzer fields are fixed at init)
    mUniforms.viewSize[0] = static_cast<float>(width);
    mUniforms.viewSize[1] = static_cast<float>(height);
    mUniforms.time = time;
    wgpuQueueWriteBuffer(mContext.getQueue(), mUniformBuffer, 0, &mUniforms, sizeof(mUniforms));
    
    // Reset state
    reset();

    // Reset pipeline
    mCurrentPipeline = mPipelines.solid;
}

void WebGPURenderer::endFrame() {
    // Finalize the last pending batch
    flushBatch();

    // A live analyzer was drawn: run this frame's FFT before the render pass is submitted.
    if (mAnalyzerRequested)
        dispatchAnalyzer();

    // Upload ALL vertices for this frame in a single write
    if (!mVertices.empty()) {
        wgpuQueueWriteBuffer(mContext.getQueue(), mVertexBuffer, 0,
                             mVertices.data(), mVertices.size() * sizeof(Vertex2D));

        // Issue all recorded draw calls
        WGPURenderPassEncoder pass = mContext.getCurrentPass();
        if (pass) {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, mBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mVertexBuffer, 0,
                                                 mVertices.size() * sizeof(Vertex2D));

            for (const auto& dc : mDrawCalls) {
                if (dc.vertexCount == 0 || !dc.pipeline) continue;
                wgpuRenderPassEncoderSetPipeline(pass, dc.pipeline);
                wgpuRenderPassEncoderDraw(pass, dc.vertexCount, 1, dc.firstVertex, 0);
            }
        }
    }

    mFrameStarted = false;
    mContext.endFrame();
}

void WebGPURenderer::save() {
    mStateStack.push_back(mCurrentState);
}

void WebGPURenderer::restore() {
    if (!mStateStack.empty()) {
        mCurrentState = mStateStack.back();
        mStateStack.pop_back();
    }
}

void WebGPURenderer::reset() {
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

void WebGPURenderer::translate(float x, float y) {
    mCurrentState.transform[4] += x;
    mCurrentState.transform[5] += y;
}

void WebGPURenderer::rotate(float angle) {
    float cs = cosf(angle);
    float sn = sinf(angle);
    float t[6];
    std::memcpy(t, mCurrentState.transform, sizeof(t));
    mCurrentState.transform[0] = t[0] * cs + t[2] * sn;
    mCurrentState.transform[1] = t[1] * cs + t[3] * sn;
    mCurrentState.transform[2] = -t[0] * sn + t[2] * cs;
    mCurrentState.transform[3] = -t[1] * sn + t[3] * cs;
}

void WebGPURenderer::scale(float x, float y) {
    mCurrentState.transform[0] *= x;
    mCurrentState.transform[1] *= x;
    mCurrentState.transform[2] *= y;
    mCurrentState.transform[3] *= y;
}

void WebGPURenderer::resetTransform() {
    mCurrentState.transform[0] = 1.0f;
    mCurrentState.transform[1] = 0.0f;
    mCurrentState.transform[2] = 0.0f;
    mCurrentState.transform[3] = 1.0f;
    mCurrentState.transform[4] = 0.0f;
    mCurrentState.transform[5] = 0.0f;
}

void WebGPURenderer::scissor(float x, float y, float w, float h) {
    mCurrentState.scissor[0] = x;
    mCurrentState.scissor[1] = y;
    mCurrentState.scissor[2] = w;
    mCurrentState.scissor[3] = h;
    mCurrentState.hasScissor = true;
}

void WebGPURenderer::resetScissor() {
    mCurrentState.hasScissor = false;
}

void WebGPURenderer::fillColor(const Color& color) {
    mCurrentState.fillColor = color;
}

void WebGPURenderer::strokeColor(const Color& color) {
    mCurrentState.strokeColor = color;
}

void WebGPURenderer::strokeWidth(float width) {
    mCurrentState.strokeWidth = width;
}

void WebGPURenderer::beginPath() {
    mPathPoints.clear();
    mPathHasStart = false;
}

void WebGPURenderer::moveTo(float x, float y) {
    transformPoint(x, y);
    mPathStartX = x;
    mPathStartY = y;
    mPathX = x;
    mPathY = y;
    mPathHasStart = true;
}

void WebGPURenderer::lineTo(float x, float y) {
    transformPoint(x, y);
    if (mPathHasStart) {
        mPathPoints.push_back(mPathX);
        mPathPoints.push_back(mPathY);
        mPathPoints.push_back(x);
        mPathPoints.push_back(y);
    }
    mPathX = x;
    mPathY = y;
}

void WebGPURenderer::closePath() {
    if (mPathHasStart) {
        lineTo(mPathStartX, mPathStartY);
    }
}

void WebGPURenderer::bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y) {
    // Approximate bezier with line segments
    const int segments = 20;
    float px = mPathX, py = mPathY;
    
    transformPoint(c1x, c1y);
    transformPoint(c2x, c2y);
    transformPoint(x, y);
    
    for (int i = 1; i <= segments; i++) {
        float t = static_cast<float>(i) / segments;
        float t2 = t * t;
        float t3 = t2 * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;
        
        float bx = mt3 * px + 3.0f * mt2 * t * c1x + 3.0f * mt * t2 * c2x + t3 * x;
        float by = mt3 * py + 3.0f * mt2 * t * c1y + 3.0f * mt * t2 * c2y + t3 * y;
        
        mPathPoints.push_back(mPathX);
        mPathPoints.push_back(mPathY);
        mPathPoints.push_back(bx);
        mPathPoints.push_back(by);
        
        mPathX = bx;
        mPathY = by;
    }
}

void WebGPURenderer::quadTo(float cx, float cy, float x, float y) {
    // Convert quadratic to cubic bezier
    float c1x = mPathX + (2.0f/3.0f) * (cx - mPathX);
    float c1y = mPathY + (2.0f/3.0f) * (cy - mPathY);
    float c2x = x + (2.0f/3.0f) * (cx - x);
    float c2y = y + (2.0f/3.0f) * (cy - y);
    bezierTo(c1x, c1y, c2x, c2y, x, y);
}

void WebGPURenderer::arc(float cx, float cy, float r, float a0, float a1, int dir) {
    transformPoint(cx, cy);
    
    // Draw arc as line segments
    float da = a1 - a0;
    // Callers use dir=0 for the natural sweep between a0 and a1, including
    // positive quarter-turns and full circles. Only normalize when a wrapped
    // clockwise sweep is explicitly requested.
    if (dir == 1 && da < 0) {
        da += TWO_PI;
    }
    
    int numSegments = std::max(3, static_cast<int>(std::abs(da) * r / kArcTessellationFactor));
    float dAngle = da / static_cast<float>(numSegments);
    
    if (!mPathHasStart) {
        float startX = cx + cosf(a0) * r;
        float startY = cy + sinf(a0) * r;
        moveTo(startX, startY);
    }
    
    for (int i = 1; i <= numSegments; i++) {
        float angle = a0 + dAngle * static_cast<float>(i);
        float x = cx + cosf(angle) * r;
        float y = cy + sinf(angle) * r;
        
        mPathPoints.push_back(mPathX);
        mPathPoints.push_back(mPathY);
        mPathPoints.push_back(x);
        mPathPoints.push_back(y);
        
        mPathX = x;
        mPathY = y;
    }
}

void WebGPURenderer::arcTo(float x1, float y1, float x2, float y2, float /*radius*/) {
    // Simplified arc-to implementation
    transformPoint(x1, y1);
    transformPoint(x2, y2);

    // Just draw lines for now
    lineTo(x1, y1);
    lineTo(x2, y2);
}

void WebGPURenderer::fill() {
    setPipeline(mPipelines.solid);

    // Simple triangle fan fill for convex shapes
    if (mPathPoints.size() < 4) return;
    
    // Get center point (average of all points)
    float cx = 0, cy = 0;
    int numPoints = 0;
    for (size_t i = 0; i < mPathPoints.size(); i += 2) {
        cx += mPathPoints[i];
        cy += mPathPoints[i + 1];
        numPoints++;
    }
    cx /= static_cast<float>(numPoints);
    cy /= static_cast<float>(numPoints);
    
    // Draw triangles from center
    for (size_t i = 0; i < mPathPoints.size() - 2; i += 2) {
        pushVertex(cx, cy, 0, 0, mCurrentState.fillColor);
        pushVertex(mPathPoints[i], mPathPoints[i + 1], 0, 0, mCurrentState.fillColor);
        pushVertex(mPathPoints[i + 2], mPathPoints[i + 3], 0, 0, mCurrentState.fillColor);
    }
}

void WebGPURenderer::stroke() {
    setPipeline(mStrokePipeline);

    // Draw path as lines
    for (size_t i = 0; i < mPathPoints.size() - 2; i += 2) {
        // Create thick line using quads
        float x1 = mPathPoints[i];
        float y1 = mPathPoints[i + 1];
        float x2 = mPathPoints[i + 2];
        float y2 = mPathPoints[i + 3];
        
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.0001f) continue;
        
        // This actually draws filled quads (triangles), not lines, so we use pushVertex
        // Note: stroke pipeline is configured as LineList but here we are generating quads.
        // If we really want thick lines in WebGPU, we must draw quads.
        // However, if mStrokePipeline is LineList, we must output lines.
        // Let's stick to simple lines for LineList, or switch to quads if we want width > 1.
        // For now, simpler implementation:
        
        // Just raw lines for LineList topology
        if (mCurrentState.strokeWidth <= 1.5f) {
             pushVertex(x1, y1, 0, 0, mCurrentState.strokeColor);
             pushVertex(x2, y2, 0, 0, mCurrentState.strokeColor);
        } else {
             // For thick lines, we need to use a triangle topology pipeline (solid)
             // But we already switched to mStrokePipeline (LineList).
             // Let's temporarily switch to solid for thick lines?
             // Or just ignore thickness for now to keep it simple and correct.
             pushVertex(x1, y1, 0, 0, mCurrentState.strokeColor);
             pushVertex(x2, y2, 0, 0, mCurrentState.strokeColor);
        }
    }
}

void WebGPURenderer::rect(float x, float y, float w, float h) {
    beginPath();
    moveTo(x, y);
    lineTo(x + w, y);
    lineTo(x + w, y + h);
    lineTo(x, y + h);
    closePath();
}

void WebGPURenderer::roundedRect(float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) * 0.5f);
    
    beginPath();
    moveTo(x + r, y);
    lineTo(x + w - r, y);
    arc(x + w - r, y + r, r, -HALF_PI, 0, 0);
    lineTo(x + w, y + h - r);
    arc(x + w - r, y + h - r, r, 0, HALF_PI, 0);
    lineTo(x + r, y + h);
    arc(x + r, y + h - r, r, HALF_PI, PI, 0);
    lineTo(x, y + r);
    arc(x + r, y + r, r, PI, PI * 1.5f, 0);
    closePath();
}

void WebGPURenderer::circle(float cx, float cy, float r) {
    beginPath();
    arc(cx, cy, r, 0, TWO_PI, 0);
    closePath();
}

void WebGPURenderer::ellipse(float cx, float cy, float rx, float ry) {
    // Draw ellipse as series of segments
    beginPath();
    const int segments = 32;
    for (int i = 0; i <= segments; i++) {
        float angle = (static_cast<float>(i) / segments) * TWO_PI;
        float x = cx + cosf(angle) * rx;
        float y = cy + sinf(angle) * ry;
        if (i == 0) {
            moveTo(x, y);
        } else {
            lineTo(x, y);
        }
    }
    closePath();
}

void WebGPURenderer::line(float x1, float y1, float x2, float y2) {
    beginPath();
    moveTo(x1, y1);
    lineTo(x2, y2);
    stroke();
}

void WebGPURenderer::fontSize(float size) {
    mFontSize = size;
}

void WebGPURenderer::fontFace(const char* name) {
    mFontName = name;
}

bool WebGPURenderer::sdfTextActive() const {
    return mPipelines.sdf_text && useSdfTextForFace(mFontName.c_str());
}

void WebGPURenderer::text(float x, float y, const char* string) {
    if (!string || string[0] == '\0') return;

    if (sdfTextActive()) {
        setPipeline(mPipelines.sdf_text);
        const Color textColor = mCurrentState.fillColor;
        uiSdfFont().layout(string, x, y, mFontSize, [&](const SdfGlyphQuad& q) {
            pushQuad(q.x0, q.y0, q.x1, q.y1, q.u0, q.v0, q.u1, q.v1, textColor);
        });
        return;
    }

    if (!mPipelines.pixel_text) return;

    const float charHeight = mFontSize;
    const float charSpacing = mFontSize * kPixelFontCharSpacingRatio;
    const Color textColor = mCurrentState.fillColor;

    setPipeline(mPipelines.pixel_text);

    float currentX = x;
    const size_t len = strlen(string);
    for (size_t i = 0; i < len; ) {
        const PixelFontGlyph glyph = pixelFontDecodeGlyph(string, i, len);
        if (glyph.byteLength == 0)
            break;

        i += glyph.byteLength;

        const float charWidth = pixelFontGlyphAdvance(glyph.index, mFontSize);
        if (glyph.index == 0) {
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

float WebGPURenderer::textWidth(const char* string) {
    if (sdfTextActive())
        return uiSdfFont().textWidth(string, mFontSize);
    return pixelFontTextWidth(string, mFontSize);
}

// ============================================================================
// Drawing Helpers
// ============================================================================

void WebGPURenderer::setPipeline(WGPURenderPipeline pipeline) {
    if (mCurrentPipeline != pipeline) {
        flushBatch();
        mCurrentPipeline = pipeline;
    }
}

void WebGPURenderer::pushQuad(float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1, const Color& color) {
    float tx1 = x0, ty1 = y0;
    float tx2 = x1, ty2 = y0;
    float tx3 = x1, ty3 = y1;
    float tx4 = x0, ty4 = y1;
    transformPoint(tx1, ty1);
    transformPoint(tx2, ty2);
    transformPoint(tx3, ty3);
    transformPoint(tx4, ty4);

    // 2 triangles
    pushVertex(tx1, ty1, u0, v0, color);
    pushVertex(tx2, ty2, u1, v0, color);
    pushVertex(tx3, ty3, u1, v1, color);
    pushVertex(tx1, ty1, u0, v0, color);
    pushVertex(tx3, ty3, u1, v1, color);
    pushVertex(tx4, ty4, u0, v1, color);
}

void WebGPURenderer::drawQuad(ShaderKind kind, float x, float y, float w, float h) {
    WGPURenderPipeline pipeline = mPipelines.solid;
    switch (kind) {
        case ShaderKind::Solid: pipeline = mPipelines.solid; break;
        case ShaderKind::KnobHighlight: pipeline = mPipelines.knob_highlight; break;
        case ShaderKind::DialTicks: pipeline = mPipelines.dial_ticks; break;
        case ShaderKind::WireGlow: pipeline = mPipelines.wire_glow; break;
        case ShaderKind::ConnectionPulse: pipeline = mPipelines.connection_pulse; break;
        case ShaderKind::SliderTrack: pipeline = mPipelines.slider_track; break;
        case ShaderKind::SliderFill: pipeline = mPipelines.slider_fill; break;
        case ShaderKind::SliderHandle: pipeline = mPipelines.slider_handle; break;
        case ShaderKind::Button: pipeline = mPipelines.button; break;
        case ShaderKind::ButtonHover: pipeline = mPipelines.button_hover; break;
        case ShaderKind::ToggleSwitch: pipeline = mPipelines.toggle_switch; break;
        case ShaderKind::ToggleThumb: pipeline = mPipelines.toggle_thumb; break;
        case ShaderKind::VUMeter: pipeline = mPipelines.vu_meter; break;
        case ShaderKind::ADSRGrid: pipeline = mPipelines.adsr_grid; break;
        case ShaderKind::PanelBackground: pipeline = mPipelines.panel_background; break;
        case ShaderKind::PanelBordered: pipeline = mPipelines.panel_bordered; break;
        case ShaderKind::LEDOn: pipeline = mPipelines.led_indicator; break;
        case ShaderKind::LEDOff: pipeline = mPipelines.led_off; break;
        case ShaderKind::SpectrumBar: pipeline = mPipelines.spectrum_bar; break;
        case ShaderKind::SpectrumPeak: pipeline = mPipelines.spectrum_peak; break;
        case ShaderKind::ProgressBar: pipeline = mPipelines.progress_bar; break;
        case ShaderKind::ModWheel: pipeline = mPipelines.mod_wheel; break;
        case ShaderKind::ScopeGrid: pipeline = mPipelines.scope_grid; break;
        case ShaderKind::FaderGroove: pipeline = mPipelines.fader_groove; break;
        case ShaderKind::FaderCap: pipeline = mPipelines.fader_cap; break;
        case ShaderKind::Count: break;
    }
    drawQuad(x, y, w, h, pipeline ? pipeline : mPipelines.solid);
}

void WebGPURenderer::drawQuad(float x, float y, float w, float h, WGPURenderPipeline pipeline) {
    setPipeline(pipeline);
    pushQuad(x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, mCurrentState.fillColor);
}

bool WebGPURenderer::drawLiveWaveform(float x, float y, float w, float h, const Color& traceColor) {
    if (!mPipelines.live_waveform || !mAnalyzerPipeline) return false;
    // fs_live_waveform only reads the sample buffer, but it is refreshed by the same upload.
    mAnalyzerRequested = true;
    setPipeline(mPipelines.live_waveform);
    pushQuad(x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, traceColor);
    return true;
}

bool WebGPURenderer::drawLiveSpectrum(float x, float y, float w, float h) {
    if (!mPipelines.live_spectrum || !mAnalyzerPipeline) return false;
    mAnalyzerRequested = true;
    setPipeline(mPipelines.live_spectrum);
    // Same base color drawSpectrum() passes to its bar/peak shaders.
    pushQuad(x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, Color(0.0f, 1.0f, 0.0f, 1.0f));
    return true;
}

void WebGPURenderer::transformPoint(float& x, float& y) {
    float tx = mCurrentState.transform[0] * x + mCurrentState.transform[2] * y + mCurrentState.transform[4];
    float ty = mCurrentState.transform[1] * x + mCurrentState.transform[3] * y + mCurrentState.transform[5];
    x = tx;
    y = ty;
}

void WebGPURenderer::pushVertex(float x, float y, float u, float v, const Color& color) {
    Vertex2D vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.u = u;
    vertex.v = v;
    vertex.color = color;
    mVertices.push_back(vertex);
}

void WebGPURenderer::flushBatch() {
    uint32_t vertexCount = static_cast<uint32_t>(mVertices.size()) - mCurrentBatchFirstVertex;
    if (vertexCount == 0) return;

    WGPURenderPipeline pipeline = mCurrentPipeline ? mCurrentPipeline : mPipelines.solid;
    mDrawCalls.push_back({ pipeline, mCurrentBatchFirstVertex, vertexCount });
    mCurrentBatchFirstVertex = static_cast<uint32_t>(mVertices.size());
}

// Factory (declared in Renderer2D.h)
std::unique_ptr<Renderer2D> createWebGPURenderer(WebGPUContext& context)
{
   return std::make_unique<WebGPURenderer>(context);
}

} // namespace wasm
} // namespace bespoke
