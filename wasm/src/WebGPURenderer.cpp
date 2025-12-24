/**
 * BespokeSynth WASM - WebGPU Renderer Implementation
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "WebGPURenderer.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace bespoke {
namespace wasm {

// Helper for StringViews
WGPUStringView s(const char* str) {
    return WGPUStringView{str, strlen(str)};
}

// Constants for 2D rendering
static const int kArcTessellationFactor = 4;  // Arc subdivisions per radius unit
static const float kCharacterWidthRatio = 0.6f;  // Character width as ratio of font size

// Shader source code (WGSL)
static const char* kVertexShader = R"(
struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) texcoord: vec2<f32>,
    @location(2) color: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) texcoord: vec2<f32>,
    @location(1) color: vec4<f32>,
};

struct Uniforms {
    viewSize: vec2<f32>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    
    // Convert from pixel coordinates to clip space (-1 to 1)
    let clipX = (input.position.x / uniforms.viewSize.x) * 2.0 - 1.0;
    let clipY = 1.0 - (input.position.y / uniforms.viewSize.y) * 2.0;
    
    output.position = vec4<f32>(clipX, clipY, 0.0, 1.0);
    output.texcoord = input.texcoord;
    output.color = input.color;
    
    return output;
}
)";

static const char* kFragmentShader = R"(
struct FragmentInput {
    @location(0) texcoord: vec2<f32>,
    @location(1) color: vec4<f32>,
};

@fragment
fn main(input: FragmentInput) -> @location(0) vec4<f32> {
    return input.color;
}
)";

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

WebGPURenderer::~WebGPURenderer() {
    if (mBindGroup) wgpuBindGroupRelease(mBindGroup);
    if (mUniformBuffer) wgpuBufferRelease(mUniformBuffer);
    if (mVertexBuffer) wgpuBufferRelease(mVertexBuffer);
    if (mStrokePipeline) wgpuRenderPipelineRelease(mStrokePipeline);
    if (mFillPipeline) wgpuRenderPipelineRelease(mFillPipeline);
}

bool WebGPURenderer::initialize() {
    if (!mContext.isInitialized()) {
        return false;
    }
    
    createPipelines();
    createBuffers();
    
    return true;
}

void WebGPURenderer::createPipelines() {
    WGPUDevice device = mContext.getDevice();
    
    // Vertex Shader
    WGPUShaderSourceWGSL vertexWGSL = {};
    vertexWGSL.chain.sType = WGPUSType_ShaderSourceWGSL;
    vertexWGSL.code = s(kVertexShader);

    WGPUShaderModuleDescriptor vertexDesc = {};
    vertexDesc.nextInChain = (WGPUChainedStruct*)&vertexWGSL;
    WGPUShaderModule vertexShader = wgpuDeviceCreateShaderModule(device, &vertexDesc);

    // Fragment Shader
    WGPUShaderSourceWGSL fragmentWGSL = {};
    fragmentWGSL.chain.sType = WGPUSType_ShaderSourceWGSL;
    fragmentWGSL.code = s(kFragmentShader);

    WGPUShaderModuleDescriptor fragmentDesc = {};
    fragmentDesc.nextInChain = (WGPUChainedStruct*)&fragmentWGSL;
    WGPUShaderModule fragmentShader = wgpuDeviceCreateShaderModule(device, &fragmentDesc);
    
    // Vertex layout
    WGPUVertexAttribute attributes[3] = {};
    
    // Position
    attributes[0].format = WGPUVertexFormat_Float32x2;
    attributes[0].offset = offsetof(Vertex2D, x);
    attributes[0].shaderLocation = 0;
    
    // Texcoord
    attributes[1].format = WGPUVertexFormat_Float32x2;
    attributes[1].offset = offsetof(Vertex2D, u);
    attributes[1].shaderLocation = 1;
    
    // Color
    attributes[2].format = WGPUVertexFormat_Float32x4;
    attributes[2].offset = offsetof(Vertex2D, color);
    attributes[2].shaderLocation = 2;
    
    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = sizeof(Vertex2D);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 3;
    vertexBufferLayout.attributes = attributes;
    
    // Bind group layout
    WGPUBindGroupLayoutEntry bindGroupLayoutEntry = {};
    bindGroupLayoutEntry.binding = 0;
    bindGroupLayoutEntry.visibility = WGPUShaderStage_Vertex;
    bindGroupLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    bindGroupLayoutEntry.buffer.minBindingSize = sizeof(float) * 2;
    
    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.entryCount = 1;
    bindGroupLayoutDesc.entries = &bindGroupLayoutEntry;
    WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);
    
    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
    
    // Create fill pipeline (triangles)
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    
    pipelineDesc.vertex.module = vertexShader;
    pipelineDesc.vertex.entryPoint = s("main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
    
    WGPUFragmentState fragmentState = {};
    fragmentState.module = fragmentShader;
    fragmentState.entryPoint = s("main");
    
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = mContext.getSwapChainFormat();
    colorTarget.writeMask = WGPUColorWriteMask_All;
    
    // Enable blending
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;
    colorTarget.blend = &blendState;
    
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;
    
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    
    WGPUMultisampleState multisample = {};
    multisample.count = 1;
    multisample.mask = ~0u;
    pipelineDesc.multisample = multisample;
    
    mFillPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    
    // Create stroke pipeline (lines)
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_LineList;
    mStrokePipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    
    // Clean up
    wgpuShaderModuleRelease(vertexShader);
    wgpuShaderModuleRelease(fragmentShader);
    wgpuBindGroupLayoutRelease(bindGroupLayout);
    wgpuPipelineLayoutRelease(pipelineLayout);
}

void WebGPURenderer::createBuffers() {
    WGPUDevice device = mContext.getDevice();
    
    // Create uniform buffer
    WGPUBufferDescriptor uniformBufferDesc = {};
    uniformBufferDesc.size = sizeof(float) * 2;  // viewSize
    uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    mUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufferDesc);
    
    // Create vertex buffer (will be resized as needed)
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.size = sizeof(Vertex2D) * 65536;  // Initial size
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    mVertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    
    // Create bind group
    WGPUBindGroupEntry bindGroupEntry = {};
    bindGroupEntry.binding = 0;
    bindGroupEntry.buffer = mUniformBuffer;
    bindGroupEntry.offset = 0;
    bindGroupEntry.size = sizeof(float) * 2;
    
    // Need to get bind group layout from pipeline
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(mFillPipeline, 0);
    
    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = layout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &bindGroupEntry;
    mBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
    
    wgpuBindGroupLayoutRelease(layout);
}

void WebGPURenderer::beginFrame(int width, int height, float pixelRatio) {
    mWidth = width;
    mHeight = height;
    mPixelRatio = pixelRatio;
    mFrameStarted = true;
    
    mVertices.clear();
    
    // Update uniform buffer with view size
    float viewSize[2] = { static_cast<float>(width), static_cast<float>(height) };
    wgpuQueueWriteBuffer(mContext.getQueue(), mUniformBuffer, 0, viewSize, sizeof(viewSize));
    
    // Reset state
    reset();
}

void WebGPURenderer::endFrame() {
    flushBatch();
    mFrameStarted = false;
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
    mCurrentState.transform[0] = t[0] * cs - t[2] * sn;
    mCurrentState.transform[1] = t[1] * cs - t[3] * sn;
    mCurrentState.transform[2] = t[0] * sn + t[2] * cs;
    mCurrentState.transform[3] = t[1] * sn + t[3] * cs;
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
    if (dir == 1) {
        if (da < 0) da += 2.0f * 3.14159265f;
    } else {
        if (da > 0) da -= 2.0f * 3.14159265f;
    }
    
    int numSegments = std::max(3, static_cast<int>(std::abs(da) * r / kArcTessellationFactor));
    float dAngle = da / numSegments;
    
    if (!mPathHasStart) {
        float startX = cx + cosf(a0) * r;
        float startY = cy + sinf(a0) * r;
        moveTo(startX, startY);
    }
    
    for (int i = 1; i <= numSegments; i++) {
        float angle = a0 + dAngle * i;
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

void WebGPURenderer::arcTo(float x1, float y1, float x2, float y2, float radius) {
    // Simplified arc-to implementation
    transformPoint(x1, y1);
    transformPoint(x2, y2);
    
    float dx1 = mPathX - x1;
    float dy1 = mPathY - y1;
    float dx2 = x2 - x1;
    float dy2 = y2 - y1;
    
    // Just draw lines for now
    lineTo(x1, y1);
    lineTo(x2, y2);
}

void WebGPURenderer::fill() {
    // Simple triangle fan fill for convex shapes
    // TODO: Implement proper triangulation for complex shapes
    if (mPathPoints.size() < 4) return;
    
    // Get center point (average of all points)
    float cx = 0, cy = 0;
    int numPoints = 0;
    for (size_t i = 0; i < mPathPoints.size(); i += 2) {
        cx += mPathPoints[i];
        cy += mPathPoints[i + 1];
        numPoints++;
    }
    cx /= numPoints;
    cy /= numPoints;
    
    // Draw triangles from center
    for (size_t i = 0; i < mPathPoints.size() - 2; i += 2) {
        pushVertex(cx, cy, 0, 0, mCurrentState.fillColor);
        pushVertex(mPathPoints[i], mPathPoints[i + 1], 0, 0, mCurrentState.fillColor);
        pushVertex(mPathPoints[i + 2], mPathPoints[i + 3], 0, 0, mCurrentState.fillColor);
    }
}

void WebGPURenderer::stroke() {
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
        
        float nx = -dy / len * mCurrentState.strokeWidth * 0.5f;
        float ny = dx / len * mCurrentState.strokeWidth * 0.5f;
        
        pushVertex(x1 + nx, y1 + ny, 0, 0, mCurrentState.strokeColor);
        pushVertex(x1 - nx, y1 - ny, 0, 0, mCurrentState.strokeColor);
        pushVertex(x2 + nx, y2 + ny, 0, 0, mCurrentState.strokeColor);
        
        pushVertex(x1 - nx, y1 - ny, 0, 0, mCurrentState.strokeColor);
        pushVertex(x2 - nx, y2 - ny, 0, 0, mCurrentState.strokeColor);
        pushVertex(x2 + nx, y2 + ny, 0, 0, mCurrentState.strokeColor);
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
    arc(x + w - r, y + r, r, -3.14159265f / 2, 0, 0);
    lineTo(x + w, y + h - r);
    arc(x + w - r, y + h - r, r, 0, 3.14159265f / 2, 0);
    lineTo(x + r, y + h);
    arc(x + r, y + h - r, r, 3.14159265f / 2, 3.14159265f, 0);
    lineTo(x, y + r);
    arc(x + r, y + r, r, 3.14159265f, 3.14159265f * 1.5f, 0);
    closePath();
}

void WebGPURenderer::circle(float cx, float cy, float r) {
    beginPath();
    arc(cx, cy, r, 0, 2.0f * 3.14159265f, 0);
    closePath();
}

void WebGPURenderer::ellipse(float cx, float cy, float rx, float ry) {
    // Draw ellipse as series of segments
    beginPath();
    const int segments = 32;
    for (int i = 0; i <= segments; i++) {
        float angle = (static_cast<float>(i) / segments) * 2.0f * 3.14159265f;
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

void WebGPURenderer::text(float x, float y, const char* string) {
    // Simple box-based text rendering as placeholder
    // Each character is a small box with approximate spacing
    if (!string || string[0] == '\0') return;
    
    float charWidth = mFontSize * kCharacterWidthRatio;
    float charHeight = mFontSize;
    float charSpacing = charWidth * 0.2f;
    
    Color textColor = mCurrentState.fillColor;
    Color dimColor(textColor.r * 0.3f, textColor.g * 0.3f, textColor.b * 0.3f, textColor.a);
    
    float currentX = x;
    size_t len = strlen(string);
    for (size_t i = 0; i < len; i++) {
        char c = string[i];
        
        // Skip spaces but add spacing
        if (c == ' ') {
            currentX += charWidth + charSpacing;
            continue;
        }
        
        // Draw character as a small box with a line to represent the glyph
        fillColor(dimColor);
        rect(currentX, y - charHeight * 0.8f, charWidth * 0.9f, charHeight * 0.9f);
        fill();
        
        // Add a simple line pattern to distinguish letters
        strokeColor(textColor);
        strokeWidth(1.0f);
        float centerX = currentX + charWidth * 0.45f;
        float centerY = y - charHeight * 0.4f;
        line(centerX, centerY - charHeight * 0.2f, centerX, centerY + charHeight * 0.2f);
        
        currentX += charWidth + charSpacing;
    }
}

float WebGPURenderer::textWidth(const char* string) {
    // Approximate text width
    return strlen(string) * mFontSize * kCharacterWidthRatio;
}

// Specialized synth UI elements

void WebGPURenderer::drawKnob(float cx, float cy, float radius, float value,
                               const Color& bgColor, const Color& fgColor) {
    // Draw background circle
    fillColor(bgColor);
    circle(cx, cy, radius);
    fill();
    
    // Draw outer ring
    strokeColor(fgColor);
    strokeWidth(2.0f);
    circle(cx, cy, radius);
    stroke();
    
    // Draw value arc
    float startAngle = 0.75f * 3.14159265f;  // Start at 135 degrees
    float endAngle = startAngle + value * 1.5f * 3.14159265f;  // 270 degree sweep
    
    strokeColor(fgColor);
    strokeWidth(3.0f);
    beginPath();
    arc(cx, cy, radius * 0.7f, startAngle, endAngle, 0);
    stroke();
    
    // Draw indicator line
    float indicatorAngle = startAngle + value * 1.5f * 3.14159265f;
    float ix1 = cx + cosf(indicatorAngle) * radius * 0.3f;
    float iy1 = cy + sinf(indicatorAngle) * radius * 0.3f;
    float ix2 = cx + cosf(indicatorAngle) * radius * 0.8f;
    float iy2 = cy + sinf(indicatorAngle) * radius * 0.8f;
    
    strokeWidth(2.0f);
    line(ix1, iy1, ix2, iy2);
}

void WebGPURenderer::drawWire(float x1, float y1, float x2, float y2,
                               const Color& color, float thickness) {
    strokeColor(color);
    strokeWidth(thickness);
    line(x1, y1, x2, y2);
}

void WebGPURenderer::drawCableWithSag(float x1, float y1, float x2, float y2,
                                       const Color& color, float thickness, float sag) {
    // Draw cable with catenary-like sag
    strokeColor(color);
    strokeWidth(thickness);
    
    beginPath();
    moveTo(x1, y1);
    
    float midX = (x1 + x2) / 2;
    float midY = (y1 + y2) / 2;
    float dist = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    float sagAmount = dist * sag;
    
    // Use quadratic bezier for sag
    float cx = midX;
    float cy = midY + sagAmount;
    
    quadTo(cx, cy, x2, y2);
    stroke();
}

void WebGPURenderer::drawSlider(float x, float y, float w, float h, float value,
                                 const Color& bgColor, const Color& fgColor) {
    // Draw background
    fillColor(bgColor);
    roundedRect(x, y, w, h, 3.0f);
    fill();
    
    // Draw filled portion
    fillColor(fgColor);
    roundedRect(x, y, w * value, h, 3.0f);
    fill();
    
    // Draw outline
    strokeColor(fgColor);
    strokeWidth(1.0f);
    roundedRect(x, y, w, h, 3.0f);
    stroke();
}

void WebGPURenderer::drawVUMeter(float x, float y, float w, float h, float level,
                                  const Color& lowColor, const Color& highColor) {
    // Draw background
    fillColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
    rect(x, y, w, h);
    fill();
    
    // Draw level segments
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
        } else {
            fillColor(Color(0.2f, 0.2f, 0.2f, 1.0f));
        }
        
        rect(x + gap, segmentY + gap/2, w - gap * 2, segmentHeight - gap);
        fill();
    }
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
    if (mVertices.empty()) return;
    
    // Upload vertices
    wgpuQueueWriteBuffer(mContext.getQueue(), mVertexBuffer, 0,
                         mVertices.data(), mVertices.size() * sizeof(Vertex2D));
    
    // Get render pass encoder
    WGPURenderPassEncoder pass = mContext.beginFrame();
    if (!pass) return;
    
    // Set pipeline and draw
    wgpuRenderPassEncoderSetPipeline(pass, mFillPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, mBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mVertexBuffer, 0, mVertices.size() * sizeof(Vertex2D));
    wgpuRenderPassEncoderDraw(pass, static_cast<uint32_t>(mVertices.size()), 1, 0, 0);
    
    mContext.endFrame();
    
    mVertices.clear();
}

} // namespace wasm
} // namespace bespoke
