/**
 * BespokeSynth WASM - WebGL2 GLSL shader sources (WGSL parity)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WebGL2Shaders.h"

namespace bespoke {
namespace wasm {

const char* gl2VertexShaderSrc()
{
   return R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec4 aColor;
uniform vec2 uViewSize;
out vec2 vTexcoord;
out vec4 vColor;
void main() {
    float clipX = (aPosition.x / uViewSize.x) * 2.0 - 1.0;
    float clipY = 1.0 - (aPosition.y / uViewSize.y) * 2.0;
    gl_Position = vec4(clipX, clipY, 0.0, 1.0);
    vTexcoord = aTexcoord;
    vColor = aColor;
}
)";
}

const char* gl2SolidFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";
}

const char* gl2KnobHighlightFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(vTexcoord, center);
    vec2 lightDir = normalize(vec2(-0.5, -0.5));
    vec2 normal = normalize(vTexcoord - center);
    float highlight = max(0.0, dot(normal, lightDir));
    vec4 color = vColor;
    color.rgb += highlight * 0.3;
    float edgeDark = smoothstep(0.3, 0.5, dist);
    color.rgb *= 1.0 - edgeDark * 0.3;
    fragColor = color;
}
)";
}

const char* gl2DialTicksFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
const float PI = 3.14159265;
const float TWO_PI = 6.28318530;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec2 center = vec2(0.5, 0.5);
    vec2 toCenter = vTexcoord - center;
    float dist = length(toCenter);
    float angle = atan(toCenter.y, toCenter.x);
    float numTicks = 11.0;
    float tickAngle = TWO_PI / numTicks;
    float startAngle = 0.75 * PI;
    float validRange = step(startAngle, angle + PI) * step(angle + PI, 2.25 * PI);
    float tickPos = fract((angle + PI) / tickAngle);
    float tickWidth = 0.05;
    float tick = smoothstep(tickWidth, 0.0, abs(tickPos - 0.5) * 2.0 - (1.0 - tickWidth));
    float innerRadius = 0.42;
    float outerRadius = 0.48;
    float inRing = step(innerRadius, dist) * step(dist, outerRadius);
    vec4 color = vColor;
    color.a *= tick * inRing * validRange;
    fragColor = color;
}
)";
}

const char* gl2WireGlowFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float dist = abs(vTexcoord.y - 0.5) * 2.0;
    float core = smoothstep(0.3, 0.0, dist);
    float glow = smoothstep(1.0, 0.0, dist) * 0.5;
    vec4 color = vColor;
    color.a *= core + glow;
    fragColor = color;
}
)";
}

const char* gl2ConnectionPulseFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
uniform float uTime;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float pulse = sin(vTexcoord.x * 10.0 - uTime * 5.0) * 0.5 + 0.5;
    vec4 color = vColor;
    color.rgb += pulse * 0.2;
    fragColor = color;
}
)";
}

const char* gl2SliderTrackFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float topShadow = smoothstep(0.0, 0.15, vTexcoord.y);
    float bottomHighlight = smoothstep(1.0, 0.85, vTexcoord.y);
    float leftShadow = smoothstep(0.0, 0.1, vTexcoord.x);
    float rightHighlight = smoothstep(1.0, 0.9, vTexcoord.x);
    vec4 color = vColor;
    float shadowAmount = (1.0 - topShadow) * 0.3 + (1.0 - leftShadow) * 0.2;
    color.rgb *= 1.0 - shadowAmount;
    float highlightAmount = (1.0 - bottomHighlight) * 0.15 + (1.0 - rightHighlight) * 0.1;
    color.rgb = min(vec3(1.0), color.rgb + highlightAmount);
    fragColor = color;
}
)";
}

const char* gl2SliderFillFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
uniform float uTime;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float gradient = 1.0 - vTexcoord.y * 0.4 + 0.2;
    float shimmer = sin(vTexcoord.x * 20.0 + uTime * 2.0) * 0.05 + 1.0;
    vec4 color = vColor;
    color.rgb = min(vec3(1.0), color.rgb * gradient * shimmer);
    fragColor = color;
}
)";
}

const char* gl2SliderHandleFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(vTexcoord, center);
    float angle = atan(vTexcoord.y - 0.5, vTexcoord.x - 0.5);
    float metallic = sin(angle * 2.0 + 1.0) * 0.15 + 0.85;
    vec2 lightDir = normalize(vec2(-0.6, -0.6));
    vec2 normal = normalize(vTexcoord - center);
    float highlight = pow(max(0.0, dot(normal, lightDir)), 2.0);
    vec4 color = vColor;
    color.rgb = min(vec3(1.0), color.rgb * metallic + highlight * 0.4);
    float edge = smoothstep(0.5, 0.45, dist);
    color.a *= edge;
    fragColor = color;
}
)";
}

const char* gl2ButtonFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    bool isPressed = vTexcoord.x > 0.9;
    float topLight;
    float bottomDark;
    if (isPressed) {
        topLight = smoothstep(0.0, 0.2, vTexcoord.y) * 0.3;
        bottomDark = smoothstep(1.0, 0.8, vTexcoord.y) * 0.2;
    } else {
        topLight = (1.0 - smoothstep(0.0, 0.2, vTexcoord.y)) * 0.25;
        bottomDark = (1.0 - smoothstep(1.0, 0.8, vTexcoord.y)) * 0.3;
    }
    vec4 color = vColor;
    color.rgb = clamp(color.rgb + topLight - bottomDark, 0.0, 1.0);
    if (isPressed) {
        color.rgb *= 0.85;
    }
    fragColor = color;
}
)";
}

const char* gl2ButtonHoverFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
uniform float uTime;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float pulse = sin(uTime * 3.0) * 0.1 + 0.9;
    float edgeX = min(vTexcoord.x, 1.0 - vTexcoord.x);
    float edgeY = min(vTexcoord.y, 1.0 - vTexcoord.y);
    float edge = min(edgeX, edgeY);
    float glow = smoothstep(0.0, 0.15, edge);
    vec4 color = vColor;
    color.rgb = min(vec3(1.0), color.rgb * pulse + (1.0 - glow) * 0.25);
    fragColor = color;
}
)";
}

const char* gl2ToggleSwitchFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float trackHeight = 0.6;
    float trackTop = 0.5 - trackHeight * 0.5;
    float trackBottom = 0.5 + trackHeight * 0.5;
    float inTrackY = step(trackTop, vTexcoord.y) * step(vTexcoord.y, trackBottom);
    vec2 leftCenter = vec2(0.15, 0.5);
    vec2 rightCenter = vec2(0.85, 0.5);
    float radius = trackHeight * 0.5;
    float inLeftCircle = step(distance(vTexcoord, leftCenter), radius);
    float inRightCircle = step(distance(vTexcoord, rightCenter), radius);
    float inMiddle = step(0.15, vTexcoord.x) * step(vTexcoord.x, 0.85) * inTrackY;
    float inTrack = max(max(inLeftCircle, inRightCircle), inMiddle);
    vec4 color = vColor;
    color.a *= inTrack;
    float shadow = smoothstep(trackTop, trackTop + 0.1, vTexcoord.y) * 0.2;
    color.rgb *= 0.8 + shadow;
    fragColor = color;
}
)";
}

const char* gl2ToggleThumbFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(vTexcoord, center);
    float gradient = 1.2 - vTexcoord.y * 0.4;
    vec2 lightDir = normalize(vec2(-0.5, -0.7));
    vec2 normal = normalize(vTexcoord - center);
    float highlight = pow(max(0.0, dot(normal, lightDir)), 1.5) * 0.4;
    vec4 color = vColor;
    color.rgb = min(vec3(1.0), color.rgb * gradient + highlight);
    float edge = smoothstep(0.5, 0.4, dist);
    color.a *= edge;
    fragColor = color;
}
)";
}

const char* gl2VUMeterFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float gradient = 1.0 - vTexcoord.y * 0.3;
    vec4 color = vColor;
    color.rgb *= gradient;
    float edgeDist = min(vTexcoord.x, 1.0 - vTexcoord.x);
    float edgeGlow = smoothstep(0.0, 0.1, edgeDist);
    color.a *= edgeGlow;
    fragColor = color;
}
)";
}

const char* gl2ADSRGridFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float gridSpacing = 0.25;
    float lineWidth = 0.01;
    float gridX = abs(fract(vTexcoord.x / gridSpacing + 0.5) - 0.5) * gridSpacing;
    float gridY = abs(fract(vTexcoord.y / gridSpacing + 0.5) - 0.5) * gridSpacing;
    float lineX = smoothstep(lineWidth, 0.0, gridX);
    float lineY = smoothstep(lineWidth, 0.0, gridY);
    float grid = max(lineX, lineY);
    vec4 color = vColor;
    color.rgb += grid * 0.15;
    fragColor = color;
}
)";
}

const char* gl2ADSREnvelopeFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float envValue = vTexcoord.y;
    float fillGradient = smoothstep(0.0, 1.0, vTexcoord.y);
    vec4 color = vColor;
    color.rgb *= 0.5 + fillGradient * 0.5;
    float curveEdge = 1.0 - envValue;
    float edgeBrightness = smoothstep(0.04, 0.0, curveEdge);
    color.rgb = min(vec3(1.0), color.rgb + edgeBrightness * 0.4);
    fragColor = color;
}
)";
}

const char* gl2PanelBackgroundFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float cornerRadius = 0.08;
    float edgeX = min(vTexcoord.x, 1.0 - vTexcoord.x);
    float edgeY = min(vTexcoord.y, 1.0 - vTexcoord.y);
    float alpha = 1.0;
    if (edgeX < cornerRadius && edgeY < cornerRadius) {
        float cornerDist = distance(vec2(edgeX, edgeY), vec2(cornerRadius, cornerRadius));
        alpha = smoothstep(cornerRadius, cornerRadius - 0.01, cornerDist);
    }
    float gradient = 1.0 - vTexcoord.y * 0.1;
    float innerShadow = min(edgeX, edgeY);
    float shadowIntensity = smoothstep(0.0, 0.05, innerShadow);
    vec4 color = vColor;
    color.rgb *= gradient * (0.9 + shadowIntensity * 0.1);
    color.a *= alpha;
    fragColor = color;
}
)";
}

const char* gl2PanelBorderedFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float borderWidth = 0.02;
    float cornerRadius = 0.06;
    float edgeX = min(vTexcoord.x, 1.0 - vTexcoord.x);
    float edgeY = min(vTexcoord.y, 1.0 - vTexcoord.y);
    float edge = min(edgeX, edgeY);
    float isBorder = step(edge, borderWidth);
    float alpha = 1.0;
    if (edgeX < cornerRadius && edgeY < cornerRadius) {
        float cornerDist = distance(vec2(edgeX, edgeY), vec2(cornerRadius, cornerRadius));
        alpha = smoothstep(cornerRadius, cornerRadius - 0.01, cornerDist);
    }
    vec4 color = vColor;
    if (isBorder > 0.5) {
        color.rgb = min(vec3(1.0), color.rgb + 0.3);
    }
    color.a *= alpha;
    fragColor = color;
}
)";
}

const char* gl2LEDOnFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(vTexcoord, center);
    float body = smoothstep(0.5, 0.4, dist);
    float innerGlow = smoothstep(0.3, 0.0, dist);
    float highlight = smoothstep(0.15, 0.0, distance(vTexcoord, vec2(0.35, 0.35))) * 0.6;
    vec4 color = vColor;
    color.rgb = min(vec3(1.0), color.rgb * (0.6 + innerGlow * 0.4) + highlight);
    color.a *= body;
    fragColor = color;
}
)";
}

const char* gl2LEDOffFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(vTexcoord, center);
    float body = smoothstep(0.5, 0.4, dist);
    float highlight = smoothstep(0.15, 0.0, distance(vTexcoord, vec2(0.35, 0.35))) * 0.3;
    vec4 color = vColor;
    color.rgb = color.rgb * 0.3 + highlight;
    color.a *= body;
    fragColor = color;
}
)";
}

const char* gl2WaveformFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float centerDist = abs(vTexcoord.y - 0.5);
    float core = smoothstep(0.02, 0.0, centerDist);
    float glow = smoothstep(0.1, 0.0, centerDist) * 0.4;
    vec4 color = vColor;
    float intensity = core + glow;
    color.a *= intensity;
    color.rgb = min(vec3(1.0), color.rgb + core * 0.3);
    fragColor = color;
}
)";
}

const char* gl2WaveformFilledFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float centerDist = abs(vTexcoord.y - 0.5) * 2.0;
    float gradient = 1.0 - centerDist * 0.5;
    vec4 color = vColor;
    color.rgb *= gradient;
    float edge = smoothstep(1.0, 0.95, centerDist);
    color.a *= edge;
    fragColor = color;
}
)";
}

const char* gl2SpectrumBarFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float heightGradient = vTexcoord.y;
    vec4 color = vColor;
    if (heightGradient > 0.8) {
        float t = (heightGradient - 0.8) / 0.2;
        color.rgb = vec3(1.0, max(0.0, 1.0 - t * 0.7), 0.1);
    } else if (heightGradient > 0.5) {
        float t = (heightGradient - 0.5) / 0.3;
        color.rgb = vec3(0.5 + t * 0.5, 1.0, 0.1);
    } else {
        color.rgb = vec3(0.2, 0.5 + heightGradient, 0.2);
    }
    float leftHighlight = smoothstep(0.0, 0.2, vTexcoord.x) * 0.2;
    float rightShadow = smoothstep(1.0, 0.8, vTexcoord.x) * 0.15;
    color.rgb = min(vec3(1.0), color.rgb + leftHighlight - rightShadow);
    float gap = smoothstep(0.0, 0.05, vTexcoord.x) * smoothstep(1.0, 0.95, vTexcoord.x);
    color.a *= gap;
    fragColor = color;
}
)";
}

const char* gl2SpectrumPeakFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float dist = abs(vTexcoord.y - 0.5);
    float core = smoothstep(0.15, 0.0, dist);
    float glow = smoothstep(0.4, 0.0, dist) * 0.3;
    vec4 color = vColor;
    color.a *= core + glow;
    fragColor = color;
}
)";
}

const char* gl2ProgressBarFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
uniform float uTime;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float stripeWidth = 0.1;
    float stripeAngle = 0.5;
    float stripePos = vTexcoord.x + vTexcoord.y * stripeAngle - uTime * 0.5;
    float stripe = fract(stripePos / stripeWidth);
    float stripePattern = smoothstep(0.4, 0.5, stripe) * smoothstep(0.6, 0.5, stripe);
    vec4 color = vColor;
    color.rgb = min(vec3(1.0), color.rgb + stripePattern * 0.15);
    float gradient = 1.0 - vTexcoord.y * 0.3;
    color.rgb *= gradient;
    fragColor = color;
}
)";
}

const char* gl2ModWheelFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
const float PI = 3.14159265;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float ridgeSpacing = 0.04;
    float ridge = sin(vTexcoord.y / ridgeSpacing * PI) * 0.5 + 0.5;
    float curveX = sin(vTexcoord.x * PI);
    float lighting = 0.6 + curveX * 0.4;
    vec4 color = vColor;
    color.rgb *= lighting * (0.85 + ridge * 0.15);
    fragColor = color;
}
)";
}

const char* gl2ScopeGridFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float majorSpacing = 0.25;
    float majorLineWidth = 0.003;
    float majorGridX = abs(fract(vTexcoord.x / majorSpacing + 0.5) - 0.5) * majorSpacing;
    float majorGridY = abs(fract(vTexcoord.y / majorSpacing + 0.5) - 0.5) * majorSpacing;
    float majorGrid = max(smoothstep(majorLineWidth, 0.0, majorGridX),
                          smoothstep(majorLineWidth, 0.0, majorGridY));
    float minorSpacing = 0.05;
    float minorLineWidth = 0.001;
    float minorGridX = abs(fract(vTexcoord.x / minorSpacing + 0.5) - 0.5) * minorSpacing;
    float minorGridY = abs(fract(vTexcoord.y / minorSpacing + 0.5) - 0.5) * minorSpacing;
    float minorGrid = max(smoothstep(minorLineWidth, 0.0, minorGridX),
                          smoothstep(minorLineWidth, 0.0, minorGridY)) * 0.3;
    float centerCross = max(smoothstep(0.005, 0.0, abs(vTexcoord.x - 0.5)),
                            smoothstep(0.005, 0.0, abs(vTexcoord.y - 0.5))) * 0.5;
    float gridIntensity = max(max(majorGrid * 0.4, minorGrid), centerCross);
    vec4 color = vColor;
    color.a *= gridIntensity;
    fragColor = color;
}
)";
}

const char* gl2FaderGrooveFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float grooveWidth = 0.15;
    float distFromCenter = abs(vTexcoord.x - 0.5);
    float inGroove = smoothstep(grooveWidth, grooveWidth - 0.02, distFromCenter);
    float inset = vTexcoord.y * 0.3;
    vec4 color = vColor;
    color.rgb *= (0.7 + inset) * inGroove;
    color.a *= inGroove;
    fragColor = color;
}
)";
}

const char* gl2FaderCapFragmentSrc()
{
   return R"(#version 300 es
precision highp float;
const float PI = 3.14159265;
const float TWO_PI = 6.28318530;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float cornerRadius = 0.1;
    float edgeX = min(vTexcoord.x, 1.0 - vTexcoord.x);
    float edgeY = min(vTexcoord.y, 1.0 - vTexcoord.y);
    float alpha = 1.0;
    if (edgeX < cornerRadius && edgeY < cornerRadius) {
        float cornerDist = distance(vec2(edgeX, edgeY), vec2(cornerRadius, cornerRadius));
        alpha = smoothstep(cornerRadius, cornerRadius - 0.02, cornerDist);
    }
    float metallic = sin(vTexcoord.x * PI) * 0.15 + 0.85;
    float highlight = (1.0 - smoothstep(0.0, 0.3, vTexcoord.y)) * 0.25;
    float gripLine = sin(vTexcoord.y / 0.12 * TWO_PI) * 0.05;
    vec4 color = vColor;
    color.rgb = min(vec3(1.0), color.rgb * metallic + highlight + gripLine);
    color.a *= alpha;
    fragColor = color;
}
)";
}

const char* gl2PixelTextFragmentPrefix()
{
   return R"(#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
const int FONT_GLYPHS = )";
}

const char* gl2PixelTextFragmentSuffix()
{
   return R"(;
uniform int uFontCols[505];
void main() {
    int charIdx = clamp(int(floor(vTexcoord.x)), 0, FONT_GLYPHS - 1);
    float localX = fract(vTexcoord.x);
    int px = clamp(int(localX * 5.0), 0, 4);
    int py = clamp(int(vTexcoord.y * 7.0), 0, 6);
    int colData = uFontCols[charIdx * 5 + px];
    int pixelOn = (colData >> py) & 1;
    if (pixelOn == 0) {
        discard;
    }
    fragColor = vColor;
}
)";
}

} // namespace wasm
} // namespace bespoke
