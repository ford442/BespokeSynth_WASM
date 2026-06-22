// BespokeSynth WASM - 2D Rendering Shader
// WebGPU Shading Language (WGSL)
//
// NOTE: The canonical shader source for the WASM build lives as the
// kRender2DShader string literal inside WebGPURenderer.cpp (embedded at runtime).
// Keep this .wgsl in sync for documentation / reference builds.
//
// Recent improvements (high-impact GUI polish):
// - Extended 5x7 pixel font with full lowercase a-z, symbols, and musical/arrow glyphs.
// - Pixel font source of truth: wasm/include/BespokeWasm/pixel_font_glyphs.inc
//   (sync to WebGPURenderer.cpp / this file via: npm run sync:font)
// - UITheme colors adopted across knobs, panels, text, and values.
// - Knob labels now include live formatted values with cyan accent.
// - Value arcs + tip dots on knobs for precise tactile feedback.

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
    // Determine if button is in pressed state (signaled by extra param in u coord)
    let isPressed = input.texcoord.x > 0.9;
    
    // 3D bevel effect - reverses when pressed
    var topLight: f32;
    var bottomDark: f32;
    if (isPressed) {
        topLight = smoothstep(0.0, 0.2, input.texcoord.y) * 0.3;
        bottomDark = smoothstep(1.0, 0.8, input.texcoord.y) * 0.2;
    } else {
        topLight = (1.0 - smoothstep(0.0, 0.2, input.texcoord.y)) * 0.25;
        bottomDark = (1.0 - smoothstep(1.0, 0.8, input.texcoord.y)) * 0.3;
    }
    
    var color = input.color;
    color.r = min(1.0, max(0.0, color.r + topLight - bottomDark));
    color.g = min(1.0, max(0.0, color.g + topLight - bottomDark));
    color.b = min(1.0, max(0.0, color.b + topLight - bottomDark));
    
    // Pressed darkening
    if (isPressed) {
        color.r = color.r * 0.85;
        color.g = color.g * 0.85;
        color.b = color.b * 0.85;
    }
    
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
    let envValue = input.texcoord.y;
    
    // Gradient fill from bottom - brighter near the envelope curve
    let fillGradient = smoothstep(0.0, 1.0, input.texcoord.y);
    
    var color = input.color;
    color.r = color.r * (0.5 + fillGradient * 0.5);
    color.g = color.g * (0.5 + fillGradient * 0.5);
    color.b = color.b * (0.5 + fillGradient * 0.5);
    
    // Brighter at the envelope curve edge
    let curveEdge = 1.0 - envValue;
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

    // Subtle dot-grid for "pro audio" tactile depth.
    // Very fine spacing (~40 dots per unit) with tiny radius so it reads as
    // texture rather than pattern.  Applied at low alpha to stay invisible at
    // a glance but add perceived depth when inspected.
    let dotSpacing = 0.04;
    let dotRadius  = 0.005;
    let cellX = fract(input.texcoord.x / dotSpacing + 0.5) - 0.5;
    let cellY = fract(input.texcoord.y / dotSpacing + 0.5) - 0.5;
    let dotDist = sqrt(cellX * cellX + cellY * cellY) * dotSpacing;
    let dot = smoothstep(dotRadius, dotRadius * 0.3, dotDist);
    // Blend dot only inside the interior (avoid dotting the shadow border area)
    let dotFade = smoothstep(0.02, 0.06, min(edgeX, edgeY));
    let dotContrib = dot * dotFade * 0.055;
    color.r = color.r + dotContrib;
    color.g = color.g + dotContrib;
    color.b = color.b + dotContrib;

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

// ============================================================================
// NEW SHADERS - Enhanced Visual Effects
// ============================================================================

// Neon glow wire shader with animated gradient
@fragment
fn fs_neon_wire(input: VertexOutput) -> @location(0) vec4<f32> {
    // Distance from center of wire
    let dist = abs(input.texcoord.y - 0.5) * 2.0;
    
    // Animated rainbow gradient along the wire
    let hue = fract(input.texcoord.x * 2.0 + uniforms.time * 0.5);
    let rainbow = vec3<f32>(
        sin(hue * TWO_PI) * 0.5 + 0.5,
        sin((hue + 0.33) * TWO_PI) * 0.5 + 0.5,
        sin((hue + 0.66) * TWO_PI) * 0.5 + 0.5
    );
    
    // Core wire
    let coreWidth = 0.2;
    let core = smoothstep(coreWidth, 0.0, dist);
    
    // Neon glow
    let glowWidth = 0.8;
    let glow = smoothstep(glowWidth, 0.0, dist) * 0.7;
    
    var color = input.color;
    let intensity = core + glow;
    color.r = min(1.0, rainbow.r * intensity * 2.0);
    color.g = min(1.0, rainbow.g * intensity * 2.0);
    color.b = min(1.0, rainbow.b * intensity * 2.0);
    color.a = color.a * min(1.0, intensity * 1.5);
    
    return color;
}

// Animated beat pulse shader
@fragment
fn fs_beat_pulse(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.5);
    let dist = distance(input.texcoord, center);
    
    // Pulsing ring effect
    let pulseSpeed = 4.0;
    let pulsePhase = fract(uniforms.time * pulseSpeed);
    let pulseDist = abs(dist - pulsePhase * 0.5);
    let pulse = smoothstep(0.1, 0.0, pulseDist);
    
    // Center glow
    let centerGlow = smoothstep(0.3, 0.0, dist);
    
    var color = input.color;
    let intensity = pulse * 0.8 + centerGlow * 0.4;
    color.r = min(1.0, color.r + intensity);
    color.g = min(1.0, color.g + intensity);
    color.b = min(1.0, color.b + intensity);
    color.a = color.a * (intensity + 0.3);
    
    return color;
}

// Gradient background shader with animated noise
@fragment
fn fs_gradient_bg(input: VertexOutput) -> @location(0) vec4<f32> {
    // Vertical gradient
    let gradient = 1.0 - input.texcoord.y * 0.8;
    
    // Simple noise for texture
    let noiseScale = 20.0;
    let noise = fract(sin(dot(input.texcoord * noiseScale, vec2<f32>(12.9898, 78.233))) * 43758.5453);
    
    // Animated subtle movement
    let animNoise = fract(noise + uniforms.time * 0.1);
    
    var color = input.color;
    color.r = color.r * gradient * (0.95 + animNoise * 0.1);
    color.g = color.g * gradient * (0.95 + animNoise * 0.1);
    color.b = color.b * gradient * (0.95 + animNoise * 0.1);
    
    return color;
}

// Metallic knob shader with brushed metal effect
@fragment
fn fs_metallic_knob(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.5);
    let toCenter = input.texcoord - center;
    let dist = length(toCenter);
    let angle = atan2(toCenter.y, toCenter.x);
    
    // Brushed metal effect using radial lines
    let radialFreq = 60.0;
    let radialLines = sin(angle * radialFreq) * 0.5 + 0.5;
    
    // Circular grooves
    let grooveFreq = 15.0;
    let grooves = sin(dist * grooveFreq * TWO_PI) * 0.5 + 0.5;
    
    // Metallic reflection based on angle
    let reflection = sin(angle * 3.0 + uniforms.time * 0.5) * 0.3 + 0.7;
    
    // Top highlight
    let lightDir = normalize(vec2<f32>(-0.3, -0.7));
    let normal = normalize(toCenter);
    let highlight = pow(max(0.0, dot(normal, lightDir)), 3.0);
    
    var color = input.color;
    let metal = (radialLines * 0.1 + grooves * 0.05 + reflection * 0.85);
    color.r = min(1.0, color.r * metal + highlight * 0.5);
    color.g = min(1.0, color.g * metal + highlight * 0.5);
    color.b = min(1.0, color.b * metal + highlight * 0.5);
    
    // Circular mask
    let edge = smoothstep(0.5, 0.48, dist);
    color.a = color.a * edge;
    
    return color;
}

// Drop shadow shader
@fragment
fn fs_drop_shadow(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.5);
    let dist = distance(input.texcoord, center);
    
    // Soft radial gradient for shadow
    let shadowSoftness = 0.3;
    let alpha = smoothstep(0.5, 0.5 - shadowSoftness, dist) * (1.0 - dist * 2.0);
    
    var color = input.color;
    color.a = color.a * alpha * 0.5;
    
    return color;
}

// Frequency spectrum circular shader
@fragment
fn fs_spectrum_circular(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.5);
    let toCenter = input.texcoord - center;
    let dist = length(toCenter);
    let angle = atan2(toCenter.y, toCenter.x);
    
    // Normalize angle to 0-1 range
    let normAngle = (angle + PI) / TWO_PI;
    
    // Create frequency bars in circular arrangement
    let freq = sin(normAngle * 20.0 + uniforms.time) * 0.5 + 0.5;
    let barWidth = 0.02;
    let inBar = step(abs(fract(normAngle * 16.0) - 0.5), barWidth * 8.0);
    
    // Radial gradient based on frequency value
    let barHeight = freq * 0.4;
    let inHeight = step(dist, 0.2 + barHeight) * step(0.2, dist);
    
    var color = input.color;
    let hue = normAngle;
    color.r = sin(hue * TWO_PI) * 0.5 + 0.5;
    color.g = sin((hue + 0.33) * TWO_PI) * 0.5 + 0.5;
    color.b = sin((hue + 0.66) * TWO_PI) * 0.5 + 0.5;
    color.a = color.a * inBar * inHeight;
    
    return color;
}

// Active control glow shader
@fragment
fn fs_active_glow(input: VertexOutput) -> @location(0) vec4<f32> {
    // Animated pulsing glow from center
    let center = vec2<f32>(0.5, 0.5);
    let dist = distance(input.texcoord, center);
    
    // Pulsing animation
    let pulse = sin(uniforms.time * 4.0) * 0.2 + 0.8;
    
    // Radial glow falloff
    let glow = smoothstep(0.5 * pulse, 0.0, dist);
    
    // Ring effect
    let ringDist = abs(dist - 0.3 * pulse);
    let ring = smoothstep(0.05, 0.0, ringDist);
    
    var color = input.color;
    let intensity = glow * 0.5 + ring * 0.8;
    color.r = min(1.0, color.r + intensity);
    color.g = min(1.0, color.g + intensity);
    color.b = min(1.0, color.b + intensity);
    color.a = color.a * (glow * 0.6 + ring * 0.4);
    
    return color;
}

// LCD display screen shader
@fragment
fn fs_lcd_screen(input: VertexOutput) -> @location(0) vec4<f32> {
    // LCD pixel grid effect
    let pixelSize = 0.005;
    let pixelX = fract(input.texcoord.x / pixelSize);
    let pixelY = fract(input.texcoord.y / pixelSize);
    
    // Grid lines between pixels
    let gridX = smoothstep(0.0, 0.3, pixelX) * smoothstep(1.0, 0.7, pixelX);
    let gridY = smoothstep(0.0, 0.3, pixelY) * smoothstep(1.0, 0.7, pixelY);
    let inPixel = gridX * gridY;
    
    // Backlight gradient
    let backlight = 0.1 + input.texcoord.y * 0.05;
    
    var color = input.color;
    color.r = color.r * inPixel * 0.8 + backlight;
    color.g = color.g * inPixel * 0.9 + backlight;
    color.b = color.b * inPixel * 0.8 + backlight;
    
    return color;
}

// Vintage VU meter with needle
@fragment
fn fs_vintage_vu(input: VertexOutput) -> @location(0) vec4<f32> {
    let center = vec2<f32>(0.5, 0.2);
    let toPoint = input.texcoord - center;
    
    // Arc shape (semi-circle)
    let angle = atan2(toPoint.y, toPoint.x);
    let inArc = step(-PI * 0.8, angle) * step(angle, -PI * 0.2);
    let dist = length(toPoint);
    let inRadius = step(0.15, dist) * step(dist, 0.45);
    
    // Tick marks
    let tickFreq = 10.0;
    let tickPos = fract((angle + PI * 0.8) / (PI * 0.6) * tickFreq);
    let inTick = step(0.85, tickPos) * step(0.4, dist) * step(dist, 0.45);
    
    // Color zones (green to yellow to red)
    let t = (angle + PI * 0.8) / (PI * 0.6);
    var zoneColor: vec3<f32>;
    if (t < 0.6) {
        zoneColor = vec3<f32>(0.2, 0.8, 0.2); // Green
    } else if (t < 0.85) {
        zoneColor = vec3<f32>(0.9, 0.9, 0.2); // Yellow
    } else {
        zoneColor = vec3<f32>(0.9, 0.2, 0.2); // Red
    }
    
    var color = input.color;
    color.r = zoneColor.r * inRadius * inArc * (1.0 - inTick * 0.7);
    color.g = zoneColor.g * inRadius * inArc * (1.0 - inTick * 0.7);
    color.b = zoneColor.b * inRadius * inArc * (1.0 - inTick * 0.7);
    color.a = color.a * inArc * inRadius;
    
    return color;
}

// XY pad / joystick control shader
@fragment
fn fs_xy_pad(input: VertexOutput) -> @location(0) vec4<f32> {
    // Grid lines
    let gridSpacing = 0.2;
    let gridWidth = 0.01;
    
    let gridX = abs(fract(input.texcoord.x / gridSpacing) - 0.5) * 2.0;
    let gridY = abs(fract(input.texcoord.y / gridSpacing) - 0.5) * 2.0;
    
    let inGridX = smoothstep(gridWidth, 0.0, abs(gridX - 0.5) * gridSpacing);
    let inGridY = smoothstep(gridWidth, 0.0, abs(gridY - 0.5) * gridSpacing);
    let grid = max(inGridX, inGridY);
    
    // Crosshair at center
    let centerDistX = abs(input.texcoord.x - 0.5);
    let centerDistY = abs(input.texcoord.y - 0.5);
    let crosshair = smoothstep(0.005, 0.0, centerDistX) + smoothstep(0.005, 0.0, centerDistY);
    
    // 3D inset effect
    let topShadow = smoothstep(0.0, 0.1, input.texcoord.y);
    let leftShadow = smoothstep(0.0, 0.1, input.texcoord.x);
    let shadow = (1.0 - topShadow) * 0.3 + (1.0 - leftShadow) * 0.2;
    
    var color = input.color;
    color.r = color.r * (1.0 - shadow) + grid * 0.15 + crosshair * 0.3;
    color.g = color.g * (1.0 - shadow) + grid * 0.15 + crosshair * 0.3;
    color.b = color.b * (1.0 - shadow) + grid * 0.15 + crosshair * 0.3;
    
    return color;
}

// ============================================================================
// PIXEL FONT TEXT RENDERING (synced from pixel_font_glyphs.inc)
// ============================================================================
// @pixel-font-glyph-data-begin
    // 5x7 bitmap font: ASCII 32-126 + extended glyphs (101 total)
    const FONT_COLS = array<u32, 505>(
        0u, 0u, 0u, 0u, 0u,
        0u, 0u, 95u, 0u, 0u,
        0u, 7u, 0u, 7u, 0u,
        20u, 127u, 20u, 127u, 20u,
        36u, 42u, 127u, 42u, 18u,
        35u, 19u, 8u, 100u, 98u,
        54u, 73u, 85u, 34u, 80u,
        0u, 5u, 3u, 0u, 0u,
        0u, 28u, 34u, 65u, 0u,
        0u, 65u, 34u, 28u, 0u,
        20u, 8u, 62u, 8u, 20u,
        8u, 8u, 62u, 8u, 8u,
        0u, 80u, 48u, 0u, 0u,
        8u, 8u, 8u, 8u, 8u,
        0u, 96u, 96u, 0u, 0u,
        32u, 16u, 8u, 4u, 2u,
        62u, 65u, 65u, 65u, 62u,
        0u, 66u, 127u, 64u, 0u,
        66u, 97u, 81u, 73u, 70u,
        33u, 65u, 69u, 75u, 49u,
        24u, 20u, 18u, 127u, 16u,
        39u, 69u, 69u, 69u, 57u,
        60u, 74u, 73u, 73u, 48u,
        1u, 113u, 9u, 5u, 3u,
        54u, 73u, 73u, 73u, 54u,
        6u, 73u, 73u, 41u, 30u,
        0u, 54u, 54u, 0u, 0u,
        0u, 86u, 54u, 0u, 0u,
        8u, 20u, 34u, 65u, 0u,
        20u, 20u, 20u, 20u, 20u,
        0u, 65u, 34u, 20u, 8u,
        2u, 1u, 81u, 9u, 6u,
        50u, 73u, 121u, 65u, 62u,
        126u, 9u, 9u, 9u, 126u,
        127u, 73u, 73u, 73u, 54u,
        62u, 65u, 65u, 65u, 34u,
        127u, 65u, 65u, 34u, 28u,
        127u, 73u, 73u, 73u, 65u,
        127u, 9u, 9u, 9u, 1u,
        62u, 65u, 73u, 73u, 122u,
        127u, 8u, 8u, 8u, 127u,
        0u, 65u, 127u, 65u, 0u,
        32u, 64u, 65u, 63u, 1u,
        127u, 8u, 20u, 34u, 65u,
        127u, 64u, 64u, 64u, 64u,
        127u, 2u, 4u, 2u, 127u,
        127u, 4u, 8u, 16u, 127u,
        62u, 65u, 65u, 65u, 62u,
        127u, 9u, 9u, 9u, 6u,
        62u, 65u, 81u, 33u, 94u,
        127u, 9u, 25u, 41u, 70u,
        70u, 73u, 73u, 73u, 49u,
        1u, 1u, 127u, 1u, 1u,
        63u, 64u, 64u, 64u, 63u,
        31u, 32u, 64u, 32u, 31u,
        63u, 64u, 56u, 64u, 63u,
        99u, 20u, 8u, 20u, 99u,
        7u, 8u, 112u, 8u, 7u,
        97u, 81u, 73u, 69u, 67u,
        0u, 127u, 65u, 65u, 0u,
        2u, 4u, 8u, 16u, 32u,
        0u, 65u, 65u, 127u, 0u,
        4u, 2u, 127u, 2u, 4u,
        64u, 64u, 64u, 64u, 64u,
        0u, 1u, 2u, 4u, 0u,
        32u, 84u, 84u, 84u, 120u,
        127u, 68u, 68u, 68u, 56u,
        56u, 68u, 68u, 68u, 40u,
        56u, 68u, 68u, 68u, 127u,
        56u, 84u, 84u, 84u, 24u,
        8u, 126u, 9u, 1u, 2u,
        12u, 82u, 82u, 82u, 62u,
        127u, 4u, 4u, 4u, 120u,
        0u, 68u, 125u, 64u, 0u,
        32u, 64u, 68u, 61u, 0u,
        127u, 16u, 40u, 68u, 0u,
        0u, 65u, 127u, 64u, 0u,
        124u, 4u, 120u, 4u, 120u,
        124u, 4u, 4u, 4u, 120u,
        56u, 68u, 68u, 68u, 56u,
        124u, 36u, 36u, 36u, 24u,
        24u, 36u, 36u, 36u, 124u,
        124u, 8u, 4u, 4u, 8u,
        72u, 84u, 84u, 84u, 36u,
        4u, 63u, 68u, 68u, 32u,
        60u, 64u, 64u, 64u, 124u,
        28u, 32u, 64u, 32u, 28u,
        60u, 64u, 48u, 64u, 60u,
        68u, 40u, 16u, 40u, 68u,
        12u, 80u, 80u, 80u, 60u,
        68u, 100u, 84u, 76u, 68u,
        0u, 127u, 65u, 65u, 0u,
        0u, 65u, 54u, 8u, 0u,
        0u, 65u, 65u, 127u, 0u,
        8u, 20u, 8u, 20u, 8u,
        2u, 7u, 2u, 0u, 0u,
        4u, 2u, 3u, 2u, 3u,
        2u, 7u, 7u, 7u, 2u,
        2u, 7u, 14u, 7u, 2u,
        7u, 2u, 2u, 0u, 0u,
        0u, 2u, 2u, 7u, 0u,
    );

    // Pixel text shader - renders 5x7 bitmap font glyphs
    // texcoord.x = float(charIndex) + localX
    // charIndex: 0-100
    @fragment
    fn fs_pixel_text(input: VertexOutput) -> @location(0) vec4<f32> {
        let charIdx = clamp(i32(floor(input.texcoord.x)), 0, 100);
        let localX = fract(input.texcoord.x);

        let px = clamp(i32(localX * 5.0), 0, 4);
        let py = clamp(i32(input.texcoord.y * 7.0), 0, 6);

        let colData = FONT_COLS[charIdx * 5 + px];
        let pixelOn = (colData >> u32(py)) & 1u;

        if (pixelOn == 0u) {
            return vec4<f32>(0.0, 0.0, 0.0, 0.0);
        }

        return input.color;
    }
// @pixel-font-glyph-data-end
