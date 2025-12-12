// BespokeSynth WASM - 2D Rendering Shader
// WebGPU Shading Language (WGSL)

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
