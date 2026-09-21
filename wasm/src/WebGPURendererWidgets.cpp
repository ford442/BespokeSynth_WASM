/**
 * BespokeSynth WASM - WebGPU Renderer: synth UI widgets
 *
 * Knobs, sliders, meters, scopes and other composite controls, built on the
 * WebGPURenderer path/quad primitives (see WebGPURenderer.cpp).
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WebGPURenderer.h"
#include "BespokeWasm/Theme.h"
#include <cmath>
#include <algorithm>

namespace bespoke {
namespace wasm {

static const float PI = 3.14159265f;
static const float TWO_PI = 6.28318530f;

// ============================================================================
// Specialized synth UI elements
// ============================================================================

void WebGPURenderer::drawKnob(float cx, float cy, float radius, float value,
                               const Color& bgColor, const Color& fgColor) {
    float size = radius * 2.0f;
    float x = cx - radius;
    float y = cy - radius;

    // Knob body (3D highlight shader)
    fillColor(bgColor);
    drawQuad(x, y, size, size, mPipelines.knob_highlight);
    
    // Ticks ring
    fillColor(fgColor);
    drawQuad(x, y, size, size, mPipelines.dial_ticks);

    // Value arc (subtle range indicator for tactile feedback)
    float startA = 0.75f * PI;
    float valA   = startA + value * 1.5f * PI;
    strokeColor(UITheme::kAccentCyan);
    strokeWidth(2.0f);
    // Draw arc using short line segments (keeps API simple, no new shader)
    const int arcSegs = 12;
    for (int i = 0; i < arcSegs; ++i) {
        float t0 = (float)i / arcSegs;
        float t1 = (float)(i + 1) / arcSegs;
        float a0 = startA + (valA - startA) * t0;
        float a1 = startA + (valA - startA) * t1;
        float r = radius * 0.88f;
        line(cx + cosf(a0) * r, cy + sinf(a0) * r,
             cx + cosf(a1) * r, cy + sinf(a1) * r);
    }

    // Indicator line + tip dot (matches enhanced Knob Classic)
    float angle = startA + value * 1.5f * PI;
    float ix  = cx + cosf(angle) * radius * 0.35f;
    float iy  = cy + sinf(angle) * radius * 0.35f;
    float ix2 = cx + cosf(angle) * radius * 0.82f;
    float iy2 = cy + sinf(angle) * radius * 0.82f;

    strokeColor(fgColor);
    strokeWidth(2.2f);
    line(ix, iy, ix2, iy2);

    fillColor(UITheme::kKnobIndicator);
    circle(ix2, iy2, radius * 0.07f);
    fill();
}

void WebGPURenderer::drawWire(float x1, float y1, float x2, float y2,
                               const Color& color, float thickness) {
    // For wire glow shader, we need a quad oriented along the line
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);
    float angle = atan2(dy, dx);

    save();
    translate(x1, y1);
    rotate(angle);

    // Draw quad for wire
    fillColor(color);
    drawQuad(0, -thickness*2, len, thickness*4, mPipelines.wire_glow);

    // Add pulse if active (simulated)
    drawQuad(0, -thickness*2, len, thickness*4, mPipelines.connection_pulse);

    restore();
}

void WebGPURenderer::drawCableWithSag(float x1, float y1, float x2, float y2,
                                       const Color& color, float thickness, float sag) {
    // Tessellate the catenary/parabola into segments to apply the wire glow shader
    const int segments = 20;
    
    float midX = (x1 + x2) / 2;
    float midY = (y1 + y2) / 2;
    float dist = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    float sagAmount = dist * sag;
    
    // Control point for quadratic bezier
    float cx = midX;
    float cy = midY + sagAmount;
    
    float prevX = x1;
    float prevY = y1;

    for (int i = 1; i <= segments; i++) {
        float t = static_cast<float>(i) / segments;
        float mt = 1.0f - t;

        // Quadratic Bezier interpolation
        float x = mt * mt * x1 + 2.0f * mt * t * cx + t * t * x2;
        float y = mt * mt * y1 + 2.0f * mt * t * cy + t * t * y2;

        drawWire(prevX, prevY, x, y, color, thickness);

        prevX = x;
        prevY = y;
    }
}

void WebGPURenderer::drawSlider(float x, float y, float w, float h, float value,
                                 const Color& bgColor, const Color& fgColor) {
    // Track
    fillColor(bgColor);
    drawQuad(x, y, w, h, mPipelines.slider_track);
    
    // Fill
    fillColor(fgColor);
    // Vertical slider assumption based on typical usage, but check dimensions
    if (h > w) {
        // Vertical
        float fillH = h * value;
        drawQuad(x, y + h - fillH, w, fillH, mPipelines.slider_fill);

        // Handle
        float handleH = w * 0.5f;
        float handleY = y + h - fillH - handleH * 0.5f;
        // Clamp
        if (handleY < y) handleY = y;
        if (handleY > y + h - handleH) handleY = y + h - handleH;

        drawQuad(x, handleY, w, handleH, mPipelines.slider_handle);
    } else {
        // Horizontal
        float fillW = w * value;
        drawQuad(x, y, fillW, h, mPipelines.slider_fill);

        // Handle
        float handleW = h * 0.5f;
        float handleX = x + fillW - handleW * 0.5f;
        // Clamp
        if (handleX < x) handleX = x;
        if (handleX > x + w - handleW) handleX = x + w - handleW;

        drawQuad(handleX, y, handleW, h, mPipelines.slider_handle);
    }
}

void WebGPURenderer::drawVUMeter(float x, float y, float w, float h, float level,
                                  const Color& lowColor, const Color& highColor) {
    // Background
    fillColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
    rect(x, y, w, h);
    fill();
    
    // We can use the fs_spectrum_bar shader or similar for the gradient bar
    // Or iterate segments like before but use proper shaders

    // Let's use fs_vu_meter for each segment
    int numSegments = 10;
    float segmentHeight = h / numSegments;
    float gap = 2.0f;
    
    for (int i = 0; i < numSegments; i++) {
        float segmentLevel = static_cast<float>(i + 1) / numSegments;
        float segmentY = y + h - (i + 1) * segmentHeight;
        
        if (segmentLevel <= level) {
            float t = static_cast<float>(i) / numSegments;
            Color c(
                lowColor.r + (highColor.r - lowColor.r) * t,
                lowColor.g + (highColor.g - lowColor.g) * t,
                lowColor.b + (highColor.b - lowColor.b) * t,
                1.0f
            );
            fillColor(c);
            drawQuad(x + gap, segmentY + gap/2, w - gap * 2, segmentHeight - gap, mPipelines.vu_meter);
        } else {
            fillColor(Color(0.15f, 0.15f, 0.15f, 1.0f));
            drawQuad(x + gap, segmentY + gap/2, w - gap * 2, segmentHeight - gap, mPipelines.vu_meter);
        }
    }
}

void WebGPURenderer::drawButton(float x, float y, float w, float h, const char* label, bool pressed, bool hover) {
    // Determine color based on state
    Color baseColor = mCurrentState.fillColor;
    if (pressed) {
        baseColor.r *= 0.8f; baseColor.g *= 0.8f; baseColor.b *= 0.8f;
    }

    fillColor(baseColor);
    drawQuad(x, y, w, h, mPipelines.button);

    if (hover) {
        // Overlay glow
        fillColor(Color(1.0f, 1.0f, 1.0f, 0.2f));
        drawQuad(x, y, w, h, mPipelines.button_hover);
    }

    // Label
    if (label) {
        float labelW = textWidth(label);
        float labelX = x + (w - labelW) * 0.5f;
        float labelY = y + h * 0.65f;
        fillColor(Color(1.0f, 1.0f, 1.0f, 0.9f));
        text(labelX, labelY, label);
    }
}

void WebGPURenderer::drawToggle(float x, float y, float w, float h, bool state) {
    fillColor(mCurrentState.fillColor);
    drawQuad(x, y, w, h, mPipelines.toggle_switch);

    // Thumb position
    float thumbSize = h;
    float thumbX = state ? (x + w - thumbSize) : x;

    fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
    drawQuad(thumbX, y, thumbSize, thumbSize, mPipelines.toggle_thumb);
}

void WebGPURenderer::drawFader(float x, float y, float w, float h, float value) {
    // Groove
    fillColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
    drawQuad(x, y, w, h, mPipelines.fader_groove);

    // Cap
    float capHeight = 30.0f;
    float capY = y + h - (h * value) - capHeight * 0.5f;
    // Clamp
    if (capY < y) capY = y;
    if (capY > y + h - capHeight) capY = y + h - capHeight;

    fillColor(Color(0.8f, 0.8f, 0.85f, 1.0f));
    drawQuad(x - 5, capY, w + 10, capHeight, mPipelines.fader_cap);
}

void WebGPURenderer::drawModWheel(float x, float y, float w, float h, float value) {
    // Wheel body
    fillColor(mCurrentState.fillColor);
    drawQuad(x, y, w, h, mPipelines.mod_wheel);
}

void WebGPURenderer::drawADSR(float x, float y, float w, float h, float a, float d, float s, float r) {
    // Background grid
    fillColor(Color(0.2f, 0.2f, 0.25f, 1.0f));
    drawQuad(x, y, w, h, mPipelines.adsr_grid);

    // We can't easily draw the envelope shape with a single quad.
    // For now, let's just draw the grid.
    // To do it properly, we'd need a custom mesh or multiple quads.
}

void WebGPURenderer::drawWaveform(float x, float y, float w, float h, const float* data, int count, bool filled) {
    // Draw background
    fillColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
    rect(x, y, w, h);
    fill();

    if (!data || count <= 0) return;

    // Draw waveform segments using lines or thin quads
    fillColor(mCurrentState.strokeColor);

    // This is expensive to do with quads for every sample
    // Ideally we would use a line strip with mStrokePipeline
    // But let's use the waveform shader on a single quad covering the area?
    // No, the waveform shader expects y-coord to be the amplitude.

    // Actually, fs_waveform uses the texcoord to draw a line at center.
    // To draw a real waveform, we need to generate geometry.
    // Let's stick to lines for now.

    beginPath();
    for (int i = 0; i < count; i++) {
        float px = x + (static_cast<float>(i) / (count - 1)) * w;
        float py = y + h * 0.5f - data[i] * h * 0.5f;
        if (i == 0) moveTo(px, py);
        else lineTo(px, py);
    }
    stroke();
}

void WebGPURenderer::drawSpectrum(float x, float y, float w, float h, const float* data, int count) {
    if (!data || count <= 0) return;

    float barWidth = w / count;

    for (int i = 0; i < count; i++) {
        float barH = data[i] * h;
        float barX = x + i * barWidth;
        float barY = y + h - barH;

        // Bar
        fillColor(Color(0.0f, 1.0f, 0.0f, 1.0f)); // Color handled by shader
        drawQuad(barX, barY, barWidth - 1, barH, mPipelines.spectrum_bar);
        
        // Peak
        drawQuad(barX, barY - 2, barWidth - 1, 2, mPipelines.spectrum_peak);
    }
}

void WebGPURenderer::drawScope(float x, float y, float w, float h, const float* data, int count) {
    // Grid
    fillColor(Color(0.0f, 0.2f, 0.0f, 1.0f));
    drawQuad(x, y, w, h, mPipelines.scope_grid);

    // Trace
    // Similar to waveform, need to draw lines
    if (!data || count <= 0) return;

    beginPath();
    for (int i = 0; i < count; i++) {
        float px = x + (static_cast<float>(i) / (count - 1)) * w;
        float py = y + h * 0.5f - data[i] * h * 0.5f;
        if (i == 0) moveTo(px, py);
        else lineTo(px, py);
    }

    strokeColor(Color(0.2f, 1.0f, 0.2f, 1.0f));
    stroke();

    // Overlay 'beam' effect?
    // We could draw a quad with fs_scope_display over the whole thing
    // to give it a phosphor glow look, but that requires rendering to texture.
}

void WebGPURenderer::drawPanel(float x, float y, float w, float h, bool bordered) {
    fillColor(mCurrentState.fillColor);
    if (bordered) {
        drawQuad(x, y, w, h, mPipelines.panel_bordered);
    } else {
        drawQuad(x, y, w, h, mPipelines.panel_background);
    }
}

void WebGPURenderer::drawLED(float x, float y, float w, float h, bool on) {
    fillColor(mCurrentState.fillColor);
    if (on) {
        drawQuad(x, y, w, h, mPipelines.led_indicator);
    } else {
        drawQuad(x, y, w, h, mPipelines.led_off);
    }
}

void WebGPURenderer::drawProgressBar(float x, float y, float w, float h, float value) {
    // Background
    fillColor(Color(0.2f, 0.2f, 0.2f, 1.0f));
    rect(x, y, w, h);
    fill();

    // Bar
    fillColor(mCurrentState.fillColor);
    drawQuad(x, y, w * value, h, mPipelines.progress_bar);
}

void WebGPURenderer::drawXYPad(float x, float y, float w, float h, float cx, float cy)
{
   drawPanel(x, y, w, h, true);
   fillColor(UITheme::kAccentCyan);
   circle(x + cx * w, y + cy * h, 5.0f);
   fill();
}

void WebGPURenderer::drawFilterResponse(float x, float y, float w, float h)
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

void WebGPURenderer::drawLFOWaveform(float x, float y, float w, float h)
{
   std::vector<float> data(64);
   for (int i = 0; i < 64; ++i)
      data[i] = sinf(static_cast<float>(i) / 64.0f * TWO_PI);
   drawWaveform(x, y, w, h, data.data(), 64, true);
}

void WebGPURenderer::drawSequencerStep(float x, float y, float w, float h, bool active)
{
   fillColor(active ? UITheme::kAccentCyan : Color(0.2f, 0.2f, 0.22f, 1.0f));
   drawQuad(x, y, w, h, mPipelines.button);
}

void WebGPURenderer::drawSpectrumWaterfall(float x, float y, float w, float h)
{
   drawPanel(x, y, w, h, true);
}

void WebGPURenderer::drawPianoKey(float x, float y, float w, float h, bool black, bool pressed)
{
   fillColor(black ? Color(0.1f, 0.1f, 0.12f, 1.0f) : Color(0.9f, 0.9f, 0.92f, 1.0f));
   if (pressed)
      fillColor(UITheme::kAccentCyan);
   drawQuad(x, y, w, h, mPipelines.solid);
}

void WebGPURenderer::drawSpectrumRainbow(float x, float y, float w, float h, float* data, int count)
{
   drawSpectrum(x, y, w, h, data, count);
}

void WebGPURenderer::drawCircularScope(float x, float y, float w, float h)
{
   drawPanel(x, y, w, h, true);
   strokeColor(UITheme::kAccentCyan);
   strokeWidth(2.0f);
   circle(x + w * 0.5f, y + h * 0.5f, std::min(w, h) * 0.35f);
   stroke();
}

void WebGPURenderer::drawEchoTrail(float x, float y, float w, float h)
{
   drawPanel(x, y, w, h, true);
}

} // namespace wasm
} // namespace bespoke
