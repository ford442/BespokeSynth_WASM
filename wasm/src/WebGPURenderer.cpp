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
static const float PI = 3.14159265f;
static const float TWO_PI = 6.28318530f;

// Shader source code (WGSL)
static const char* kRender2DShader = R"(
// BespokeSynth WASM - 2D Rendering Shader
// WebGPU Shading Language (WGSL)

// Mathematical constants
const PI: f32 = 3.14159265;
const TWO_PI: f32 = 6.28318530;
const HALF_PI: f32 = 1.57079632;

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
    time: f32,
    padding: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

// Vertex shader for basic 2D rendering
@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    
    // Convert from pixel coordinates to clip space (-1 to 1)
    let clipX = (input.position.x / uniforms.viewSize.x) * 2.0 - 1.0;
    let clipY = 1.0 - (input.position.y / uniforms.viewSize.y) * 2.0;
    
    output.position = vec4<f32>(clipX, clipY, 0.0, 1.0);
    output.texcoord = input.texcoord;
    output.color = input.color;
    
    return output;
}

// Fragment shader for solid color
@fragment
fn fs_solid(input: VertexOutput) -> @location(0) vec4<f32> {
    return input.color;
}

// Fragment shader with texture
@group(0) @binding(1) var textureSampler: sampler;
@group(0) @binding(2) var textureData: texture_2d<f32>;

@fragment
fn fs_textured(input: VertexOutput) -> @location(0) vec4<f32> {
    let texColor = textureSample(textureData, textureSampler, input.texcoord);
    return texColor * input.color;
}

// Knob highlight shader
@fragment
fn fs_knob_highlight(input: VertexOutput) -> @location(0) vec4<f32> {
    // Create radial gradient for 3D effect
    let center = vec2<f32>(0.5, 0.5);
    let dist = distance(input.texcoord, center);

    // Highlight at top-left
    let lightDir = normalize(vec2<f32>(-0.5, -0.5));
    let normal = normalize(input.texcoord - center);
    let highlight = max(0.0, dot(normal, lightDir));

    var color = input.color;
    color.r = color.r + highlight * 0.3;
    color.g = color.g + highlight * 0.3;
    color.b = color.b + highlight * 0.3;

    // Darken at edges
    let edgeDark = smoothstep(0.3, 0.5, dist);
    color.r = color.r * (1.0 - edgeDark * 0.3);
    color.g = color.g * (1.0 - edgeDark * 0.3);
    color.b = color.b * (1.0 - edgeDark * 0.3);

    // Circle mask
    let alpha = smoothstep(0.5, 0.48, dist);
    color.a = color.a * alpha;

    return color;
}

// Wire/cable shader with glow
@fragment
fn fs_wire_glow(input: VertexOutput) -> @location(0) vec4<f32> {
    // Distance from center of wire (v = 0.5 is center)
    let dist = abs(input.texcoord.y - 0.5) * 2.0;

    // Core wire
    let coreWidth = 0.3;
    let core = smoothstep(coreWidth, 0.0, dist);

    // Glow
    let glowWidth = 1.0;
    let glow = smoothstep(glowWidth, 0.0, dist) * 0.5;

    var color = input.color;
    color.a = color.a * (core + glow);

    return color;
}

// VU meter segment shader
@fragment
fn fs_vu_meter(input: VertexOutput) -> @location(0) vec4<f32> {
    // Add subtle gradient
    let gradient = 1.0 - input.texcoord.y * 0.3;

    var color = input.color;
    color.r = color.r * gradient;
    color.g = color.g * gradient;
    color.b = color.b * gradient;

    // Add subtle glow at edges
    let edgeDist = min(input.texcoord.x, 1.0 - input.texcoord.x);
    let edgeGlow = smoothstep(0.0, 0.1, edgeDist);
    color.a = color.a * edgeGlow;

    return color;
}

// Animated connection pulse
@fragment
fn fs_connection_pulse(input: VertexOutput) -> @location(0) vec4<f32> {
    // Animate along the wire
    let pulse = sin(input.texcoord.x * 10.0 - uniforms.time * 5.0) * 0.5 + 0.5;

    var color = input.color;
    color.r = color.r + pulse * 0.2;
    color.g = color.g + pulse * 0.2;
    color.b = color.b + pulse * 0.2;

    return color;
}

// ============================================================================
// CONTROL PANEL SHADERS
// ============================================================================

// Slider track shader with gradient and 3D inset effect
@fragment
fn fs_slider_track(input: VertexOutput) -> @location(0) vec4<f32> {
    // Create 3D inset effect
    let topShadow = smoothstep(0.0, 0.15, input.texcoord.y);
    let bottomHighlight = smoothstep(1.0, 0.85, input.texcoord.y);
    let leftShadow = smoothstep(0.0, 0.1, input.texcoord.x);
    let rightHighlight = smoothstep(1.0, 0.9, input.texcoord.x);

    var color = input.color;
    // Apply inset shadow at top and left
    let shadowAmount = (1.0 - topShadow) * 0.3 + (1.0 - leftShadow) * 0.2;
    color.r = color.r * (1.0 - shadowAmount);
    color.g = color.g * (1.0 - shadowAmount);
    color.b = color.b * (1.0 - shadowAmount);

    // Apply highlight at bottom and right
    let highlightAmount = (1.0 - bottomHighlight) * 0.15 + (1.0 - rightHighlight) * 0.1;
    color.r = min(1.0, color.r + highlightAmount);
    color.g = min(1.0, color.g + highlightAmount);
    color.b = min(1.0, color.b + highlightAmount);

    return color;
}

// Slider fill shader with animated gradient
@fragment
fn fs_slider_fill(input: VertexOutput) -> @location(0) vec4<f32> {
    // Vertical gradient for 3D raised effect
    let gradient = 1.0 - input.texcoord.y * 0.4 + 0.2;

    // Subtle horizontal shimmer animation
    let shimmer = sin(input.texcoord.x * 20.0 + uniforms.time * 2.0) * 0.05 + 1.0;

    var color = input.color;
    color.r = min(1.0, color.r * gradient * shimmer);
    color.g = min(1.0, color.g * gradient * shimmer);
    color.b = min(1.0, color.b * gradient * shimmer);

    return color;
}

// Slider handle/thumb shader with metallic look
@fragment
fn fs_slider_handle(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.5);
    let dist = distance(input.texcoord, center);

    // Metallic gradient based on angle
    let angle = atan2(input.texcoord.y - 0.5, input.texcoord.x - 0.5);
    let metallic = sin(angle * 2.0 + 1.0) * 0.15 + 0.85;

    // Top-left highlight for 3D effect
    let lightDir = normalize(vec2<f32>(-0.6, -0.6));
    let normal = normalize(input.texcoord - center);
    let highlight = pow(max(0.0, dot(normal, lightDir)), 2.0);

    var color = input.color;
    color.r = min(1.0, color.r * metallic + highlight * 0.4);
    color.g = min(1.0, color.g * metallic + highlight * 0.4);
    color.b = min(1.0, color.b * metallic + highlight * 0.4);

    // Circular mask with soft edge
    let edge = smoothstep(0.5, 0.45, dist);
    color.a = color.a * edge;

    return color;
}

// Button shader with pressed state support
// Use texcoord.x > 0.5 for pressed state indication
@fragment
fn fs_button(input: VertexOutput) -> @location(0) vec4<f32> {
    // Determine if button is in pressed state (signaled by extra param in u coord - simulated here by simple logic or uniform,
    // but in this shader we'll assume standard UV and rely on color or other cues, OR we repurpose UVs.
    // Actually, let's just use standard bevel.)

    // 3D bevel effect
    var topLight: f32 = (1.0 - smoothstep(0.0, 0.2, input.texcoord.y)) * 0.25;
    var bottomDark: f32 = (1.0 - smoothstep(1.0, 0.8, input.texcoord.y)) * 0.3;

    var color = input.color;
    color.r = min(1.0, max(0.0, color.r + topLight - bottomDark));
    color.g = min(1.0, max(0.0, color.g + topLight - bottomDark));
    color.b = min(1.0, max(0.0, color.b + topLight - bottomDark));

    return color;
}

// Button hover glow effect
@fragment
fn fs_button_hover(input: VertexOutput) -> @location(0) vec4<f32> {
    // Pulsing glow effect
    let pulse = sin(uniforms.time * 3.0) * 0.1 + 0.9;

    // Edge glow
    let edgeX = min(input.texcoord.x, 1.0 - input.texcoord.x);
    let edgeY = min(input.texcoord.y, 1.0 - input.texcoord.y);
    let edge = min(edgeX, edgeY);
    let glow = smoothstep(0.0, 0.15, edge);

    var color = input.color;
    color.r = min(1.0, color.r * pulse + (1.0 - glow) * 0.2);
    color.g = min(1.0, color.g * pulse + (1.0 - glow) * 0.2);
    color.b = min(1.0, color.b * pulse + (1.0 - glow) * 0.3);

    return color;
}

// Toggle switch shader
@fragment
fn fs_toggle_switch(input: VertexOutput) -> @location(0) vec4<f32> {
    // Track background with rounded ends
    let trackHeight = 0.6;
    let trackTop = 0.5 - trackHeight * 0.5;
    let trackBottom = 0.5 + trackHeight * 0.5;

    let inTrackY = step(trackTop, input.texcoord.y) * step(input.texcoord.y, trackBottom);

    // Rounded ends using circles at left and right
    let leftCenter = vec2<f32>(0.15, 0.5);
    let rightCenter = vec2<f32>(0.85, 0.5);
    let radius = trackHeight * 0.5;

    let inLeftCircle = step(distance(input.texcoord, leftCenter), radius);
    let inRightCircle = step(distance(input.texcoord, rightCenter), radius);
    let inMiddle = step(0.15, input.texcoord.x) * step(input.texcoord.x, 0.85) * inTrackY;

    let inTrack = max(max(inLeftCircle, inRightCircle), inMiddle);

    var color = input.color;
    color.a = color.a * inTrack;

    // Subtle 3D inset
    let shadow = smoothstep(trackTop, trackTop + 0.1, input.texcoord.y) * 0.2;
    color.r = color.r * (0.8 + shadow);
    color.g = color.g * (0.8 + shadow);
    color.b = color.b * (0.8 + shadow);

    return color;
}

// Toggle switch thumb/handle
@fragment
fn fs_toggle_thumb(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.5);
    let dist = distance(input.texcoord, center);

    // Circular thumb with gradient
    let gradient = 1.2 - input.texcoord.y * 0.4;

    // Highlight
    let lightDir = normalize(vec2<f32>(-0.5, -0.7));
    let normal = normalize(input.texcoord - center);
    let highlight = pow(max(0.0, dot(normal, lightDir)), 1.5) * 0.4;

    var color = input.color;
    color.r = min(1.0, color.r * gradient + highlight);
    color.g = min(1.0, color.g * gradient + highlight);
    color.b = min(1.0, color.b * gradient + highlight);

    // Soft circular edge
    let edge = smoothstep(0.5, 0.4, dist);
    color.a = color.a * edge;

    return color;
}

// ADSR Envelope display shader
@fragment
fn fs_adsr_envelope(input: VertexOutput) -> @location(0) vec4<f32> {
    // Envelope visualization - filled area below curve
    // texcoord.x represents position along envelope, texcoord.y represents the envelope curve value at that position
    let envValue = input.texcoord.y;

    // Gradient fill from bottom - brighter near the envelope curve
    let fillGradient = smoothstep(0.0, 1.0, input.texcoord.y);

    var color = input.color;
    color.r = color.r * (0.5 + fillGradient * 0.5);
    color.g = color.g * (0.5 + fillGradient * 0.5);
    color.b = color.b * (0.5 + fillGradient * 0.5);

    // Brighter at the envelope curve edge (where y approaches the actual envelope value)
    // The envelope value is passed via the v texture coordinate, highlight pixels near that boundary
    let curveEdge = 1.0 - envValue; // distance from top of fill to top of screen
    let edgeBrightness = smoothstep(0.04, 0.0, curveEdge);
    color.r = min(1.0, color.r + edgeBrightness * 0.4);
    color.g = min(1.0, color.g + edgeBrightness * 0.4);
    color.b = min(1.0, color.b + edgeBrightness * 0.4);

    return color;
}

// ADSR grid/background shader
@fragment
fn fs_adsr_grid(input: VertexOutput) -> @location(0) vec4<f32> {
    // Grid lines
    let gridSpacing = 0.25;
    let lineWidth = 0.01;

    let gridX = abs(fract(input.texcoord.x / gridSpacing + 0.5) - 0.5) * gridSpacing;
    let gridY = abs(fract(input.texcoord.y / gridSpacing + 0.5) - 0.5) * gridSpacing;

    let lineX = smoothstep(lineWidth, 0.0, gridX);
    let lineY = smoothstep(lineWidth, 0.0, gridY);
    let grid = max(lineX, lineY);

    var color = input.color;
    color.r = color.r + grid * 0.15;
    color.g = color.g + grid * 0.15;
    color.b = color.b + grid * 0.15;

    return color;
}

// Waveform display shader
@fragment
fn fs_waveform(input: VertexOutput) -> @location(0) vec4<f32> {
    // Centered waveform - calculate distance from center line
    let centerDist = abs(input.texcoord.y - 0.5);

    // Waveform thickness with glow
    let coreWidth = 0.02;
    let glowWidth = 0.1;

    let core = smoothstep(coreWidth, 0.0, centerDist);
    let glow = smoothstep(glowWidth, 0.0, centerDist) * 0.4;

    var color = input.color;
    let intensity = core + glow;
    color.a = color.a * intensity;

    // Brighter core
    color.r = min(1.0, color.r + core * 0.3);
    color.g = min(1.0, color.g + core * 0.3);
    color.b = min(1.0, color.b + core * 0.3);

    return color;
}

// Waveform filled display (for audio visualization)
@fragment
fn fs_waveform_filled(input: VertexOutput) -> @location(0) vec4<f32> {
    // Gradient from center outward
    let centerDist = abs(input.texcoord.y - 0.5) * 2.0;
    let gradient = 1.0 - centerDist * 0.5;

    var color = input.color;
    color.r = color.r * gradient;
    color.g = color.g * gradient;
    color.b = color.b * gradient;

    // Soft edge at amplitude boundary
    let edge = smoothstep(1.0, 0.95, centerDist);
    color.a = color.a * edge;

    return color;
}

// Spectrum analyzer bar shader
@fragment
fn fs_spectrum_bar(input: VertexOutput) -> @location(0) vec4<f32> {
    // Vertical gradient - brighter at top
    let heightGradient = input.texcoord.y;

    // Color gradient from green to yellow to red based on height
    var color = input.color;
    if (heightGradient > 0.8) {
        // Red zone (high amplitude)
        let t = (heightGradient - 0.8) / 0.2;
        color.r = 1.0;
        color.g = max(0.0, 1.0 - t * 0.7);
        color.b = 0.1;
    } else if (heightGradient > 0.5) {
        // Yellow zone
        let t = (heightGradient - 0.5) / 0.3;
        color.r = 0.5 + t * 0.5;
        color.g = 1.0;
        color.b = 0.1;
    } else {
        // Green zone
        color.r = 0.2;
        color.g = 0.5 + heightGradient;
        color.b = 0.2;
    }

    // 3D raised effect
    let leftHighlight = smoothstep(0.0, 0.2, input.texcoord.x) * 0.2;
    let rightShadow = smoothstep(1.0, 0.8, input.texcoord.x) * 0.15;
    color.r = min(1.0, color.r + leftHighlight - rightShadow);
    color.g = min(1.0, color.g + leftHighlight - rightShadow);
    color.b = min(1.0, color.b + leftHighlight - rightShadow);

    // Slight gap between bars
    let gap = smoothstep(0.0, 0.05, input.texcoord.x) * smoothstep(1.0, 0.95, input.texcoord.x);
    color.a = color.a * gap;

    return color;
}

// Spectrum analyzer peak hold indicator
@fragment
fn fs_spectrum_peak(input: VertexOutput) -> @location(0) vec4<f32> {
    // Thin horizontal line with glow
    let centerY = 0.5;
    let dist = abs(input.texcoord.y - centerY);

    let core = smoothstep(0.15, 0.0, dist);
    let glow = smoothstep(0.4, 0.0, dist) * 0.3;

    var color = input.color;
    color.a = color.a * (core + glow);

    return color;
}

// Panel background with rounded corners shader
@fragment
fn fs_panel_background(input: VertexOutput) -> @location(0) vec4<f32> {
    // Rounded corner calculation (assuming square panel)
    let cornerRadius = 0.08;
    let edgeX = min(input.texcoord.x, 1.0 - input.texcoord.x);
    let edgeY = min(input.texcoord.y, 1.0 - input.texcoord.y);

    // Check if in corner region
    var alpha = 1.0;
    if (edgeX < cornerRadius && edgeY < cornerRadius) {
        let cornerDist = distance(
            vec2<f32>(edgeX, edgeY),
            vec2<f32>(cornerRadius, cornerRadius)
        );
        alpha = smoothstep(cornerRadius, cornerRadius - 0.01, cornerDist);
    }

    // Subtle gradient for depth
    let gradient = 1.0 - input.texcoord.y * 0.1;

    // Inner shadow at edges
    let innerShadow = min(edgeX, edgeY);
    let shadowIntensity = smoothstep(0.0, 0.05, innerShadow);

    var color = input.color;
    color.r = color.r * gradient * (0.9 + shadowIntensity * 0.1);
    color.g = color.g * gradient * (0.9 + shadowIntensity * 0.1);
    color.b = color.b * gradient * (0.9 + shadowIntensity * 0.1);
    color.a = color.a * alpha;

    return color;
}

// Panel with border/outline shader
@fragment
fn fs_panel_bordered(input: VertexOutput) -> @location(0) vec4<f32> {
    let borderWidth = 0.02;
    let cornerRadius = 0.06;

    let edgeX = min(input.texcoord.x, 1.0 - input.texcoord.x);
    let edgeY = min(input.texcoord.y, 1.0 - input.texcoord.y);
    let edge = min(edgeX, edgeY);

    // Border detection
    let isBorder = step(edge, borderWidth);

    // Corner rounding
    var alpha = 1.0;
    if (edgeX < cornerRadius && edgeY < cornerRadius) {
        let cornerDist = distance(
            vec2<f32>(edgeX, edgeY),
            vec2<f32>(cornerRadius, cornerRadius)
        );
        alpha = smoothstep(cornerRadius, cornerRadius - 0.01, cornerDist);
    }

    var color = input.color;
    // Border is brighter
    if (isBorder > 0.5) {
        color.r = min(1.0, color.r + 0.3);
        color.g = min(1.0, color.g + 0.3);
        color.b = min(1.0, color.b + 0.3);
    }
    color.a = color.a * alpha;

    return color;
}

// Text glow effect shader
@fragment
fn fs_text_glow(input: VertexOutput) -> @location(0) vec4<f32> {
    // Assumes text is rendered with alpha channel
    // Adds outer glow based on alpha

    var color = input.color;

    // Pulsing glow for emphasis
    let pulse = sin(uniforms.time * 2.0) * 0.15 + 0.85;

    // Distance-based glow (simulated - actual text needs distance field)
    let glowIntensity = color.a * pulse;

    color.r = min(1.0, color.r + glowIntensity * 0.2);
    color.g = min(1.0, color.g + glowIntensity * 0.2);
    color.b = min(1.0, color.b + glowIntensity * 0.3);

    return color;
}

// Text shadow shader
@fragment
fn fs_text_shadow(input: VertexOutput) -> @location(0) vec4<f32> {
    var color = vec4<f32>(0.0, 0.0, 0.0, input.color.a * 0.5);

    // Soft shadow falloff
    let shadowFalloff = smoothstep(1.0, 0.0, input.texcoord.y);
    color.a = color.a * shadowFalloff;

    return color;
}

// Progress bar shader
@fragment
fn fs_progress_bar(input: VertexOutput) -> @location(0) vec4<f32> {
    // Animated stripes for progress indication
    let stripeWidth = 0.1;
    let stripeAngle = 0.5; // 45 degrees approximately

    let stripePos = input.texcoord.x + input.texcoord.y * stripeAngle - uniforms.time * 0.5;
    let stripe = fract(stripePos / stripeWidth);
    let stripePattern = smoothstep(0.4, 0.5, stripe) * smoothstep(0.6, 0.5, stripe);

    var color = input.color;
    color.r = min(1.0, color.r + stripePattern * 0.15);
    color.g = min(1.0, color.g + stripePattern * 0.15);
    color.b = min(1.0, color.b + stripePattern * 0.15);

    // Vertical gradient for 3D effect
    let gradient = 1.0 - input.texcoord.y * 0.3;
    color.r = color.r * gradient;
    color.g = color.g * gradient;
    color.b = color.b * gradient;

    return color;
}

// Oscilloscope/scope display shader
@fragment
fn fs_scope_display(input: VertexOutput) -> @location(0) vec4<f32> {
    // Phosphor glow effect like old CRT oscilloscope
    let centerDist = abs(input.texcoord.y - 0.5);

    // Beam core (bright)
    let beamWidth = 0.015;
    let beam = smoothstep(beamWidth, 0.0, centerDist);

    // Phosphor glow (wider, dimmer)
    let glowWidth = 0.08;
    let glow = smoothstep(glowWidth, 0.0, centerDist) * 0.3;

    // Afterglow trail (very wide, very dim)
    let trailWidth = 0.15;
    let trail = smoothstep(trailWidth, 0.0, centerDist) * 0.1;

    let intensity = beam + glow + trail;

    var color = input.color;
    // Phosphor green tint
    color.r = color.r * intensity * 0.3;
    color.g = color.g * intensity;
    color.b = color.b * intensity * 0.4;
    color.a = color.a * intensity;

    return color;
}

// Scope grid overlay shader
@fragment
fn fs_scope_grid(input: VertexOutput) -> @location(0) vec4<f32> {
    // Major grid lines
    let majorSpacing = 0.25;
    let majorLineWidth = 0.003;

    let majorGridX = abs(fract(input.texcoord.x / majorSpacing + 0.5) - 0.5) * majorSpacing;
    let majorGridY = abs(fract(input.texcoord.y / majorSpacing + 0.5) - 0.5) * majorSpacing;

    let majorLineX = smoothstep(majorLineWidth, 0.0, majorGridX);
    let majorLineY = smoothstep(majorLineWidth, 0.0, majorGridY);
    let majorGrid = max(majorLineX, majorLineY);

    // Minor grid lines
    let minorSpacing = 0.05;
    let minorLineWidth = 0.001;

    let minorGridX = abs(fract(input.texcoord.x / minorSpacing + 0.5) - 0.5) * minorSpacing;
    let minorGridY = abs(fract(input.texcoord.y / minorSpacing + 0.5) - 0.5) * minorSpacing;

    let minorLineX = smoothstep(minorLineWidth, 0.0, minorGridX);
    let minorLineY = smoothstep(minorLineWidth, 0.0, minorGridY);
    let minorGrid = max(minorLineX, minorLineY) * 0.3;

    // Center crosshair (brighter)
    let centerX = smoothstep(0.005, 0.0, abs(input.texcoord.x - 0.5));
    let centerY = smoothstep(0.005, 0.0, abs(input.texcoord.y - 0.5));
    let centerCross = max(centerX, centerY) * 0.5;

    let gridIntensity = max(max(majorGrid * 0.4, minorGrid), centerCross);

    var color = input.color;
    color.a = color.a * gridIntensity;

    return color;
}

// LED indicator shader
@fragment
fn fs_led_indicator(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.5);
    let dist = distance(input.texcoord, center);

    // LED body
    let body = smoothstep(0.5, 0.4, dist);

    // Inner glow (lit state)
    let innerGlow = smoothstep(0.3, 0.0, dist);

    // Highlight reflection
    let highlightPos = vec2<f32>(0.35, 0.35);
    let highlightDist = distance(input.texcoord, highlightPos);
    let highlight = smoothstep(0.15, 0.0, highlightDist) * 0.6;

    var color = input.color;
    color.r = min(1.0, color.r * (0.6 + innerGlow * 0.4) + highlight);
    color.g = min(1.0, color.g * (0.6 + innerGlow * 0.4) + highlight);
    color.b = min(1.0, color.b * (0.6 + innerGlow * 0.4) + highlight);
    color.a = color.a * body;

    return color;
}

// LED indicator off state shader
@fragment
fn fs_led_off(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.5);
    let dist = distance(input.texcoord, center);

    // LED body (darker when off)
    let body = smoothstep(0.5, 0.4, dist);

    // Subtle highlight even when off
    let highlightPos = vec2<f32>(0.35, 0.35);
    let highlightDist = distance(input.texcoord, highlightPos);
    let highlight = smoothstep(0.15, 0.0, highlightDist) * 0.3;

    var color = input.color;
    color.r = color.r * 0.3 + highlight;
    color.g = color.g * 0.3 + highlight;
    color.b = color.b * 0.3 + highlight;
    color.a = color.a * body;

    return color;
}

// Rotary encoder/dial tick marks shader
@fragment
fn fs_dial_ticks(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.5);
    let toCenter = input.texcoord - center;
    let dist = length(toCenter);
    let angle = atan2(toCenter.y, toCenter.x);

    // Draw tick marks around the dial
    let numTicks = 11.0;
    let tickAngle = TWO_PI / numTicks;

    // Only draw in valid angle range (270 degrees, from 135 to 405 degrees)
    let startAngle = 0.75 * PI;
    let validRange = step(startAngle, angle + PI) * step(angle + PI, 2.25 * PI);

    // Tick positions
    let tickPos = fract((angle + PI) / tickAngle);
    let tickWidth = 0.05;
    let tick = smoothstep(tickWidth, 0.0, abs(tickPos - 0.5) * 2.0 - (1.0 - tickWidth));

    // Tick visible in outer ring
    let innerRadius = 0.42;
    let outerRadius = 0.48;
    let inRing = step(innerRadius, dist) * step(dist, outerRadius);

    var color = input.color;
    color.a = color.a * tick * inRing;

    return color;
}

// Fader groove/slot shader
@fragment
fn fs_fader_groove(input: VertexOutput) -> @location(0) vec4<f32> {
    // Narrow vertical groove with 3D inset effect
    let grooveWidth = 0.15;
    let centerX = 0.5;

    let distFromCenter = abs(input.texcoord.x - centerX);
    let inGroove = smoothstep(grooveWidth, grooveWidth - 0.02, distFromCenter);

    // 3D inset - dark at top of groove, light at bottom
    let inset = input.texcoord.y * 0.3;

    var color = input.color;
    color.r = color.r * (0.7 + inset) * inGroove;
    color.g = color.g * (0.7 + inset) * inGroove;
    color.b = color.b * (0.7 + inset) * inGroove;
    color.a = color.a * inGroove;

    return color;
}

// Fader cap/handle shader
@fragment
fn fs_fader_cap(input: VertexOutput) -> @location(0) vec4<f32> {
    // Rectangular cap with rounded edges and metallic look
    let cornerRadius = 0.1;
    let edgeX = min(input.texcoord.x, 1.0 - input.texcoord.x);
    let edgeY = min(input.texcoord.y, 1.0 - input.texcoord.y);

    // Rounded corners
    var alpha = 1.0;
    if (edgeX < cornerRadius && edgeY < cornerRadius) {
        let cornerDist = distance(
            vec2<f32>(edgeX, edgeY),
            vec2<f32>(cornerRadius, cornerRadius)
        );
        alpha = smoothstep(cornerRadius, cornerRadius - 0.02, cornerDist);
    }

    // Metallic horizontal gradient
    let metallic = sin(input.texcoord.x * PI) * 0.15 + 0.85;

    // Vertical highlight at top
    let highlight = (1.0 - smoothstep(0.0, 0.3, input.texcoord.y)) * 0.25;

    // Grip lines (horizontal ridges)
    let gripSpacing = 0.12;
    let gripLine = sin(input.texcoord.y / gripSpacing * TWO_PI) * 0.05;

    var color = input.color;
    color.r = min(1.0, color.r * metallic + highlight + gripLine);
    color.g = min(1.0, color.g * metallic + highlight + gripLine);
    color.b = min(1.0, color.b * metallic + highlight + gripLine);
    color.a = color.a * alpha;

    return color;
}

// Modulation wheel shader
@fragment
fn fs_mod_wheel(input: VertexOutput) -> @location(0) vec4<f32> {
    // Wheel texture with horizontal ridges
    let ridgeSpacing = 0.04;
    let ridge = sin(input.texcoord.y / ridgeSpacing * PI) * 0.5 + 0.5;

    // Curve effect for 3D cylinder appearance
    let curveX = sin(input.texcoord.x * PI);
    let lighting = 0.6 + curveX * 0.4;

    var color = input.color;
    color.r = color.r * lighting * (0.85 + ridge * 0.15);
    color.g = color.g * lighting * (0.85 + ridge * 0.15);
    color.b = color.b * lighting * (0.85 + ridge * 0.15);

    return color;
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

    // Release all pipelines
    if (mStrokePipeline) wgpuRenderPipelineRelease(mStrokePipeline);
    if (mPipelines.solid) wgpuRenderPipelineRelease(mPipelines.solid);
    if (mPipelines.textured) wgpuRenderPipelineRelease(mPipelines.textured);
    if (mPipelines.knob_highlight) wgpuRenderPipelineRelease(mPipelines.knob_highlight);
    if (mPipelines.wire_glow) wgpuRenderPipelineRelease(mPipelines.wire_glow);
    if (mPipelines.vu_meter) wgpuRenderPipelineRelease(mPipelines.vu_meter);
    if (mPipelines.connection_pulse) wgpuRenderPipelineRelease(mPipelines.connection_pulse);
    if (mPipelines.slider_track) wgpuRenderPipelineRelease(mPipelines.slider_track);
    if (mPipelines.slider_fill) wgpuRenderPipelineRelease(mPipelines.slider_fill);
    if (mPipelines.slider_handle) wgpuRenderPipelineRelease(mPipelines.slider_handle);
    if (mPipelines.button) wgpuRenderPipelineRelease(mPipelines.button);
    if (mPipelines.button_hover) wgpuRenderPipelineRelease(mPipelines.button_hover);
    if (mPipelines.toggle_switch) wgpuRenderPipelineRelease(mPipelines.toggle_switch);
    if (mPipelines.toggle_thumb) wgpuRenderPipelineRelease(mPipelines.toggle_thumb);
    if (mPipelines.adsr_envelope) wgpuRenderPipelineRelease(mPipelines.adsr_envelope);
    if (mPipelines.adsr_grid) wgpuRenderPipelineRelease(mPipelines.adsr_grid);
    if (mPipelines.waveform) wgpuRenderPipelineRelease(mPipelines.waveform);
    if (mPipelines.waveform_filled) wgpuRenderPipelineRelease(mPipelines.waveform_filled);
    if (mPipelines.spectrum_bar) wgpuRenderPipelineRelease(mPipelines.spectrum_bar);
    if (mPipelines.spectrum_peak) wgpuRenderPipelineRelease(mPipelines.spectrum_peak);
    if (mPipelines.panel_background) wgpuRenderPipelineRelease(mPipelines.panel_background);
    if (mPipelines.panel_bordered) wgpuRenderPipelineRelease(mPipelines.panel_bordered);
    if (mPipelines.text_glow) wgpuRenderPipelineRelease(mPipelines.text_glow);
    if (mPipelines.text_shadow) wgpuRenderPipelineRelease(mPipelines.text_shadow);
    if (mPipelines.progress_bar) wgpuRenderPipelineRelease(mPipelines.progress_bar);
    if (mPipelines.scope_display) wgpuRenderPipelineRelease(mPipelines.scope_display);
    if (mPipelines.scope_grid) wgpuRenderPipelineRelease(mPipelines.scope_grid);
    if (mPipelines.led_indicator) wgpuRenderPipelineRelease(mPipelines.led_indicator);
    if (mPipelines.led_off) wgpuRenderPipelineRelease(mPipelines.led_off);
    if (mPipelines.dial_ticks) wgpuRenderPipelineRelease(mPipelines.dial_ticks);
    if (mPipelines.fader_groove) wgpuRenderPipelineRelease(mPipelines.fader_groove);
    if (mPipelines.fader_cap) wgpuRenderPipelineRelease(mPipelines.fader_cap);
    if (mPipelines.mod_wheel) wgpuRenderPipelineRelease(mPipelines.mod_wheel);
}

bool WebGPURenderer::initialize() {
    if (!mContext.isInitialized()) {
        return false;
    }
    
    createPipelines();
    createBuffers();
    
    // Set default pipeline
    mCurrentPipeline = mPipelines.solid;

    return true;
}

void WebGPURenderer::createPipelines() {
    WGPUDevice device = mContext.getDevice();
    
    // Create shader module containing both vertex and fragment shaders
    WGPUShaderSourceWGSL shaderWGSL = {};
    shaderWGSL.chain.sType = WGPUSType_ShaderSourceWGSL;
    shaderWGSL.code = s(kRender2DShader);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = (WGPUChainedStruct*)&shaderWGSL;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    
    // Vertex layout
    WGPUVertexAttribute attributes[3] = {};
    attributes[0].format = WGPUVertexFormat_Float32x2; // position
    attributes[0].offset = offsetof(Vertex2D, x);
    attributes[0].shaderLocation = 0;
    
    attributes[1].format = WGPUVertexFormat_Float32x2; // texcoord
    attributes[1].offset = offsetof(Vertex2D, u);
    attributes[1].shaderLocation = 1;
    
    attributes[2].format = WGPUVertexFormat_Float32x4; // color
    attributes[2].offset = offsetof(Vertex2D, color);
    attributes[2].shaderLocation = 2;
    
    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = sizeof(Vertex2D);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 3;
    vertexBufferLayout.attributes = attributes;
    
    // Bind group layout (Uniforms at 0)
    WGPUBindGroupLayoutEntry bindGroupLayoutEntry = {};
    bindGroupLayoutEntry.binding = 0;
    bindGroupLayoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bindGroupLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    bindGroupLayoutEntry.buffer.minBindingSize = sizeof(float) * 4; // viewSize(2) + time(1) + padding(1)
    
    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.entryCount = 1;
    bindGroupLayoutDesc.entries = &bindGroupLayoutEntry;
    WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);
    
    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
    
    // Base pipeline descriptor
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = s("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
    
    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = mContext.getSwapChainFormat();
    colorTarget.writeMask = WGPUColorWriteMask_All;
    
    // Enable alpha blending
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

    // Helper to create pipelines
    auto createPipeline = [&](const char* entryPoint) {
        fragmentState.entryPoint = s(entryPoint);
        return wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    };
    
    // Create all pipelines
    mPipelines.solid = createPipeline("fs_solid");
    // mPipelines.textured = createPipeline("fs_textured"); // Requires texture bindings, skipping for now
    mPipelines.knob_highlight = createPipeline("fs_knob_highlight");
    mPipelines.wire_glow = createPipeline("fs_wire_glow");
    mPipelines.vu_meter = createPipeline("fs_vu_meter");
    mPipelines.connection_pulse = createPipeline("fs_connection_pulse");
    mPipelines.slider_track = createPipeline("fs_slider_track");
    mPipelines.slider_fill = createPipeline("fs_slider_fill");
    mPipelines.slider_handle = createPipeline("fs_slider_handle");
    mPipelines.button = createPipeline("fs_button");
    mPipelines.button_hover = createPipeline("fs_button_hover");
    mPipelines.toggle_switch = createPipeline("fs_toggle_switch");
    mPipelines.toggle_thumb = createPipeline("fs_toggle_thumb");
    mPipelines.adsr_envelope = createPipeline("fs_adsr_envelope");
    mPipelines.adsr_grid = createPipeline("fs_adsr_grid");
    mPipelines.waveform = createPipeline("fs_waveform");
    mPipelines.waveform_filled = createPipeline("fs_waveform_filled");
    mPipelines.spectrum_bar = createPipeline("fs_spectrum_bar");
    mPipelines.spectrum_peak = createPipeline("fs_spectrum_peak");
    mPipelines.panel_background = createPipeline("fs_panel_background");
    mPipelines.panel_bordered = createPipeline("fs_panel_bordered");
    mPipelines.text_glow = createPipeline("fs_text_glow");
    mPipelines.text_shadow = createPipeline("fs_text_shadow");
    mPipelines.progress_bar = createPipeline("fs_progress_bar");
    mPipelines.scope_display = createPipeline("fs_scope_display");
    mPipelines.scope_grid = createPipeline("fs_scope_grid");
    mPipelines.led_indicator = createPipeline("fs_led_indicator");
    mPipelines.led_off = createPipeline("fs_led_off");
    mPipelines.dial_ticks = createPipeline("fs_dial_ticks");
    mPipelines.fader_groove = createPipeline("fs_fader_groove");
    mPipelines.fader_cap = createPipeline("fs_fader_cap");
    mPipelines.mod_wheel = createPipeline("fs_mod_wheel");
    
    // Create stroke pipeline (lines) - uses solid color shader
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_LineList;
    fragmentState.entryPoint = s("fs_solid");
    mStrokePipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    
    // Clean up
    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bindGroupLayout);
    wgpuPipelineLayoutRelease(pipelineLayout);
}

void WebGPURenderer::createBuffers() {
    WGPUDevice device = mContext.getDevice();
    
    // Create uniform buffer
    WGPUBufferDescriptor uniformBufferDesc = {};
    uniformBufferDesc.size = sizeof(float) * 4;  // viewSize + time + padding
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
    bindGroupEntry.size = sizeof(float) * 4;
    
    // Need to get bind group layout from any pipeline (they all share same layout)
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(mPipelines.solid, 0);
    
    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = layout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &bindGroupEntry;
    mBindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
    
    wgpuBindGroupLayoutRelease(layout);
}

void WebGPURenderer::beginFrame(int width, int height, float pixelRatio, float time) {
    mWidth = width;
    mHeight = height;
    mPixelRatio = pixelRatio;
    mTime = time;
    mFrameStarted = true;
    
    mVertices.clear();
    
    // Update uniform buffer with view size and time
    float uniforms[4] = {
        static_cast<float>(width),
        static_cast<float>(height),
        time,
        0.0f // padding
    };
    wgpuQueueWriteBuffer(mContext.getQueue(), mUniformBuffer, 0, uniforms, sizeof(uniforms));
    
    // Reset state
    reset();

    // Reset pipeline
    mCurrentPipeline = mPipelines.solid;
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
        if (da < 0) da += TWO_PI;
    } else {
        if (da > 0) da -= TWO_PI;
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
        
        float nx = -dy / len * mCurrentState.strokeWidth * 0.5f;
        float ny = dx / len * mCurrentState.strokeWidth * 0.5f;
        
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
        // Use text_shadow for background
        drawQuad(currentX, y - charHeight * 0.8f, charWidth * 0.9f, charHeight * 0.9f, mPipelines.text_shadow);
        
        // Draw text glow
        fillColor(textColor);
        drawQuad(currentX, y - charHeight * 0.8f, charWidth * 0.9f, charHeight * 0.9f, mPipelines.text_glow);
        
        currentX += charWidth + charSpacing;
    }
}

float WebGPURenderer::textWidth(const char* string) {
    // Approximate text width
    return strlen(string) * mFontSize * kCharacterWidthRatio;
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

void WebGPURenderer::drawQuad(float x, float y, float w, float h, WGPURenderPipeline pipeline) {
    setPipeline(pipeline);

    float x1 = x;
    float y1 = y;
    float x2 = x + w;
    float y2 = y + h;

    // Transform coordinates
    float tx1 = x1, ty1 = y1;
    float tx2 = x2, ty2 = y1;
    float tx3 = x2, ty3 = y2;
    float tx4 = x1, ty4 = y2;

    transformPoint(tx1, ty1);
    transformPoint(tx2, ty2);
    transformPoint(tx3, ty3);
    transformPoint(tx4, ty4);

    // Push vertices (2 triangles)
    // Triangle 1
    pushVertex(tx1, ty1, 0.0f, 0.0f, mCurrentState.fillColor);
    pushVertex(tx2, ty2, 1.0f, 0.0f, mCurrentState.fillColor);
    pushVertex(tx3, ty3, 1.0f, 1.0f, mCurrentState.fillColor);

    // Triangle 2
    pushVertex(tx1, ty1, 0.0f, 0.0f, mCurrentState.fillColor);
    pushVertex(tx3, ty3, 1.0f, 1.0f, mCurrentState.fillColor);
    pushVertex(tx4, ty4, 0.0f, 1.0f, mCurrentState.fillColor);
}

// ============================================================================
// Specialized synth UI elements
// ============================================================================

void WebGPURenderer::drawKnob(float cx, float cy, float radius, float value,
                               const Color& bgColor, const Color& fgColor) {
    float size = radius * 2.0f;
    float x = cx - radius;
    float y = cy - radius;

    // Knob body
    fillColor(bgColor);
    drawQuad(x, y, size, size, mPipelines.knob_highlight);
    
    // Ticks ring
    fillColor(fgColor);
    drawQuad(x, y, size, size, mPipelines.dial_ticks);
    
    // Value indicator (simple line for now as the shader handles 3D look)
    // We could add a dedicated shader for the indicator if needed
    float angle = 0.75f * PI + value * 1.5f * PI;
    float ix = cx + cosf(angle) * radius * 0.6f;
    float iy = cy + sinf(angle) * radius * 0.6f;
    float ix2 = cx + cosf(angle) * radius * 0.9f;
    float iy2 = cy + sinf(angle) * radius * 0.9f;
    
    strokeColor(fgColor);
    strokeWidth(3.0f);
    line(ix, iy, ix2, iy2);
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
    if (mCurrentPipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, mCurrentPipeline);
    } else {
        wgpuRenderPassEncoderSetPipeline(pass, mPipelines.solid);
    }

    wgpuRenderPassEncoderSetBindGroup(pass, 0, mBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mVertexBuffer, 0, mVertices.size() * sizeof(Vertex2D));
    wgpuRenderPassEncoderDraw(pass, static_cast<uint32_t>(mVertices.size()), 1, 0, 0);
    
    mContext.endFrame();
    
    mVertices.clear();
}

} // namespace wasm
} // namespace bespoke
