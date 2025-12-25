/**
 * BespokeSynth WASM - WebGPU Renderer
 * Provides 2D rendering primitives using WebGPU for UI elements
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "WebGPUContext.h"
#include <vector>
#include <string>
#include <cstdint>
#include <map>

namespace bespoke {
namespace wasm {

// Color structure for WebGPU rendering
struct Color {
    float r, g, b, a;
    
    Color() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
    Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}
    
    static Color fromRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }
};

// Vertex structure for 2D rendering
struct Vertex2D {
    float x, y;
    float u, v;
    Color color;
};

// Shader Pipelines storage
struct Pipelines {
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
};

/**
 * WebGPU-based 2D renderer
 * Provides NanoVG-like API for drawing UI elements
 */
class WebGPURenderer {
public:
    WebGPURenderer(WebGPUContext& context);
    ~WebGPURenderer();
    
    // Initialize the renderer (create pipelines, buffers, etc.)
    bool initialize();
    
    // Begin/end frame
    void beginFrame(int width, int height, float pixelRatio, float time = 0.0f);
    void endFrame();
    
    // State management
    void save();
    void restore();
    void reset();
    
    // Transform operations
    void translate(float x, float y);
    void rotate(float angle);
    void scale(float x, float y);
    void resetTransform();
    
    // Scissor/clip
    void scissor(float x, float y, float w, float h);
    void resetScissor();
    
    // Style
    void fillColor(const Color& color);
    void strokeColor(const Color& color);
    void strokeWidth(float width);
    
    // Path operations
    void beginPath();
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y);
    void quadTo(float cx, float cy, float x, float y);
    void arc(float cx, float cy, float r, float a0, float a1, int dir);
    void arcTo(float x1, float y1, float x2, float y2, float radius);
    void closePath();
    
    // Drawing
    void fill();
    void stroke();
    
    // Shapes
    void rect(float x, float y, float w, float h);
    void roundedRect(float x, float y, float w, float h, float r);
    void circle(float cx, float cy, float r);
    void ellipse(float cx, float cy, float rx, float ry);
    void line(float x1, float y1, float x2, float y2);
    
    // Text (basic support)
    void fontSize(float size);
    void fontFace(const char* name);
    void text(float x, float y, const char* string);
    float textWidth(const char* string);
    
    // Specialized synth UI elements
    void drawKnob(float cx, float cy, float radius, float value, const Color& bgColor, const Color& fgColor);
    void drawWire(float x1, float y1, float x2, float y2, const Color& color, float thickness = 2.0f);
    void drawCableWithSag(float x1, float y1, float x2, float y2, const Color& color, float thickness = 3.0f, float sag = 0.3f);
    void drawSlider(float x, float y, float w, float h, float value, const Color& bgColor, const Color& fgColor);
    void drawVUMeter(float x, float y, float w, float h, float level, const Color& lowColor, const Color& highColor);

    // New UI elements
    void drawButton(float x, float y, float w, float h, const char* label, bool pressed, bool hover);
    void drawToggle(float x, float y, float w, float h, bool state);
    void drawFader(float x, float y, float w, float h, float value);
    void drawModWheel(float x, float y, float w, float h, float value);
    void drawADSR(float x, float y, float w, float h, float a, float d, float s, float r);
    void drawWaveform(float x, float y, float w, float h, const float* data, int count, bool filled);
    void drawSpectrum(float x, float y, float w, float h, const float* data, int count);
    void drawScope(float x, float y, float w, float h, const float* data, int count);
    void drawPanel(float x, float y, float w, float h, bool bordered);
    void drawLED(float x, float y, float w, float h, bool on);
    void drawProgressBar(float x, float y, float w, float h, float value);

private:
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
    
    std::vector<Vertex2D> mVertices;
    
    // State stack
    struct State {
        float transform[6];  // 2D affine transform matrix
        Color fillColor;
        Color strokeColor;
        float strokeWidth;
        float scissor[4];  // x, y, w, h
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
