/**
 * BespokeSynth WASM - WebGL2 Renderer Implementation
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "WebGL2Renderer.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace bespoke {
namespace wasm {

const float WebGL2Renderer::kPI = 3.14159265f;

// ---------------------------------------------------------------------------
// GLSL ES 3.00 shader sources
// ---------------------------------------------------------------------------

// Vertex shader shared by all programs
static const char* kGL2VertexShader = R"(#version 300 es
precision highp float;
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec4 a_color;
uniform vec2 u_viewSize;
out vec2 v_uv;
out vec4 v_color;
void main() {
    float cx = (a_pos.x / u_viewSize.x) * 2.0 - 1.0;
    float cy = 1.0 - (a_pos.y / u_viewSize.y) * 2.0;
    gl_Position = vec4(cx, cy, 0.0, 1.0);
    v_uv    = a_uv;
    v_color = a_color;
}
)";

// Solid colour fill
static const char* kGL2FragSolid = R"(#version 300 es
precision mediump float;
in vec4 v_color;
out vec4 fragColor;
void main() { fragColor = v_color; }
)";

// 5×7 pixel font — same algorithm as WGSL fs_pixel_text, using a 1D texture
// Each glyph is stored as 5 packed bytes (1 byte per column, bit0=top).
// The texture has 85*5 = 425 texels.
static const char* kGL2FragPixelText = R"(#version 300 es
precision mediump float;
in vec2  v_uv;
in vec4  v_color;
uniform sampler2D u_fontTex;
out vec4 fragColor;
void main() {
    int charIdx = int(v_uv.x);
    float localX = v_uv.x - float(charIdx);
    int px = clamp(int(localX * 5.0), 0, 4);
    int py = clamp(int(v_uv.y * 7.0), 0, 6);
    int texelIdx = charIdx * 5 + px;
    // R channel of the font texture holds the column byte
    float colByte = texture(u_fontTex, vec2((float(texelIdx) + 0.5) / 425.0, 0.5)).r * 255.0;
    int colData = int(colByte);
    int bit = (colData >> py) & 1;
    if (bit == 0) discard;
    fragColor = v_color;
}
)";

// Knob highlight — radial gradient for a 3-D dome appearance
static const char* kGL2FragKnob = R"(#version 300 es
precision mediump float;
in vec2 v_uv;
in vec4 v_color;
out vec4 fragColor;
void main() {
    vec2 center = vec2(0.5, 0.5);
    float dist   = distance(v_uv, center);
    if (dist > 0.5) discard;
    vec2 light   = normalize(vec2(-0.5, -0.5));
    vec2 n       = (v_uv - center) * 2.0;
    float diffuse = max(0.0, dot(normalize(n), light));
    float edge    = 1.0 - smoothstep(0.4, 0.5, dist);
    float rim     = smoothstep(0.3, 0.5, dist) * (1.0 - smoothstep(0.45, 0.5, dist));
    vec3 col      = v_color.rgb * (0.4 + 0.6 * diffuse) + vec3(rim * 0.5);
    fragColor = vec4(col * edge, v_color.a * edge);
}
)";

// Slider track fill (solid, but used to tint the filled portion differently)
static const char* kGL2FragSlider = R"(#version 300 es
precision mediump float;
in vec2 v_uv;
in vec4 v_color;
out vec4 fragColor;
void main() { fragColor = v_color; }
)";

// Cable/wire with a soft glow edge
static const char* kGL2FragCable = R"(#version 300 es
precision mediump float;
in vec2 v_uv;
in vec4 v_color;
out vec4 fragColor;
void main() {
    float dy = v_uv.y - 0.5;
    float glow = 1.0 - smoothstep(0.0, 0.5, abs(dy) * 2.0);
    glow = glow * glow;
    fragColor = vec4(v_color.rgb, v_color.a * glow);
}
)";

// VU meter — colour gradient green→red depending on UV.y
static const char* kGL2FragVU = R"(#version 300 es
precision mediump float;
in vec2 v_uv;
in vec4 v_color;
out vec4 fragColor;
void main() {
    float t   = v_uv.y;
    vec3 col  = mix(v_color.rgb, vec3(1.0, 0.2, 0.1), smoothstep(0.7, 1.0, t));
    float seg = step(0.05, fract(t * 14.0));
    fragColor = vec4(col * seg, v_color.a);
}
)";

// ---------------------------------------------------------------------------
// Font glyph table (must match the WGSL FONT_COLS data)
// ---------------------------------------------------------------------------

static const uint8_t kFontCols[425] = {
    // 32 ' '
    0,0,0,0,0,
    // 33 '!'
    0,0,95,0,0,
    // 34 '"'
    0,7,0,7,0,
    // 35 '#'
    20,127,20,127,20,
    // 36 '$'
    36,42,127,42,18,
    // 37 '%'
    35,19,8,100,98,
    // 38 '&'
    54,73,85,34,80,
    // 39 '\''
    0,5,3,0,0,
    // 40 '('
    0,28,34,65,0,
    // 41 ')'
    0,65,34,28,0,
    // 42 '*'
    20,8,62,8,20,
    // 43 '+'
    8,8,62,8,8,
    // 44 ','
    0,80,48,0,0,
    // 45 '-'
    8,8,8,8,8,
    // 46 '.'
    0,96,96,0,0,
    // 47 '/'
    32,16,8,4,2,
    // 48 '0'
    62,65,65,65,62,
    // 49 '1'
    0,66,127,64,0,
    // 50 '2'
    66,97,81,73,70,
    // 51 '3'
    33,65,69,75,49,
    // 52 '4'
    24,20,18,127,16,
    // 53 '5'
    39,69,69,69,57,
    // 54 '6'
    60,74,73,73,48,
    // 55 '7'
    1,113,9,5,3,
    // 56 '8'
    54,73,73,73,54,
    // 57 '9'
    6,73,73,41,30,
    // 58 ':'
    0,54,54,0,0,
    // 59 ';'
    0,86,54,0,0,
    // 60 '<'
    8,20,34,65,0,
    // 61 '='
    20,20,20,20,20,
    // 62 '>'
    0,65,34,20,8,
    // 63 '?'
    2,1,81,9,6,
    // 64 '@'
    50,73,121,65,62,
    // 65 'A'
    126,9,9,9,126,
    // 66 'B'
    127,73,73,73,54,
    // 67 'C'
    62,65,65,65,34,
    // 68 'D'
    127,65,65,34,28,
    // 69 'E'
    127,73,73,73,65,
    // 70 'F'
    127,9,9,9,1,
    // 71 'G'
    62,65,73,73,122,
    // 72 'H'
    127,8,8,8,127,
    // 73 'I'
    0,65,127,65,0,
    // 74 'J'
    32,64,65,63,1,
    // 75 'K'
    127,8,20,34,65,
    // 76 'L'
    127,64,64,64,64,
    // 77 'M'
    127,2,4,2,127,
    // 78 'N'
    127,4,8,16,127,
    // 79 'O'
    62,65,65,65,62,
    // 80 'P'
    127,9,9,9,6,
    // 81 'Q'
    62,65,81,33,94,
    // 82 'R'
    127,9,25,41,70,
    // 83 'S'
    70,73,73,73,49,
    // 84 'T'
    1,1,127,1,1,
    // 85 'U'
    63,64,64,64,63,
    // 86 'V'
    31,32,64,32,31,
    // 87 'W'
    63,64,56,64,63,
    // 88 'X'
    99,20,8,20,99,
    // 89 'Y'
    7,8,112,8,7,
    // 90 'Z'
    97,81,73,69,67,
    // --- Lowercase a-z (indices 59-84) ---
    // 97 'a'
    32,84,84,84,120,
    // 98 'b'
    127,68,68,68,56,
    // 99 'c'
    56,68,68,68,0,
    // 100 'd'
    56,68,68,68,127,
    // 101 'e'
    56,92,84,84,88,
    // 102 'f'
    4,126,5,1,0,
    // 103 'g'
    12,82,82,82,62,
    // 104 'h'
    127,4,4,4,120,
    // 105 'i'
    0,0,125,0,0,
    // 106 'j'
    32,64,64,61,0,
    // 107 'k'
    127,8,20,34,64,
    // 108 'l'
    0,1,127,64,0,
    // 109 'm'
    124,6,124,6,124,
    // 110 'n'
    124,4,4,4,120,
    // 111 'o'
    56,68,68,68,56,
    // 112 'p'
    124,18,18,18,14,
    // 113 'q'
    28,18,18,18,126,
    // 114 'r'
    120,4,4,8,0,
    // 115 's'
    8,84,84,84,32,
    // 116 't'
    4,63,68,68,0,
    // 117 'u'
    60,64,64,64,124,
    // 118 'v'
    28,32,64,32,28,
    // 119 'w'
    60,64,112,64,60,
    // 120 'x'
    68,40,16,40,68,
    // 121 'y'
    12,80,80,80,60,
    // 122 'z'
    68,100,84,76,68,
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

GLuint WebGL2Renderer::compileShader(GLenum type, const char* src)
{
#ifdef __EMSCRIPTEN__
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        printf("WebGL2Renderer: shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
#else
    (void)type; (void)src;
    return 0;
#endif
}

GLuint WebGL2Renderer::linkProgram(GLuint vert, GLuint frag)
{
#ifdef __EMSCRIPTEN__
    if (!vert || !frag) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        printf("WebGL2Renderer: program link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
#else
    (void)vert; (void)frag;
    return 0;
#endif
}

void WebGL2Renderer::setUniformViewSize(GLuint prog) const
{
#ifdef __EMSCRIPTEN__
    GLint loc = glGetUniformLocation(prog, "u_viewSize");
    if (loc >= 0)
        glUniform2f(loc, static_cast<float>(mWidth), static_cast<float>(mHeight));
#else
    (void)prog;
#endif
}

void WebGL2Renderer::buildFontTexture()
{
#ifdef __EMSCRIPTEN__
    glGenTextures(1, &mFontTexture);
    glBindTexture(GL_TEXTURE_2D, mFontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Upload as a 425×1 R8 texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 425, 1, 0, GL_RED, GL_UNSIGNED_BYTE, kFontCols);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

WebGL2Renderer::WebGL2Renderer()
{
    mCurrentState.transform[0] = 1.0f;
    mCurrentState.transform[1] = 0.0f;
    mCurrentState.transform[2] = 0.0f;
    mCurrentState.transform[3] = 1.0f;
    mCurrentState.transform[4] = 0.0f;
    mCurrentState.transform[5] = 0.0f;
    mCurrentState.fillColor   = Color(1.0f, 1.0f, 1.0f, 1.0f);
    mCurrentState.strokeColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
    mCurrentState.strokeWidth = 1.0f;
}

WebGL2Renderer::~WebGL2Renderer()
{
#ifdef __EMSCRIPTEN__
    if (mVBO) glDeleteBuffers(1, &mVBO);
    if (mVAO) glDeleteVertexArrays(1, &mVAO);
    if (mProgSolid)     glDeleteProgram(mProgSolid);
    if (mProgPixelText) glDeleteProgram(mProgPixelText);
    if (mProgKnob)      glDeleteProgram(mProgKnob);
    if (mProgSlider)    glDeleteProgram(mProgSlider);
    if (mProgCable)     glDeleteProgram(mProgCable);
    if (mProgVU)        glDeleteProgram(mProgVU);
    if (mFontTexture)   glDeleteTextures(1, &mFontTexture);
#endif
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------

bool WebGL2Renderer::initialize()
{
#ifndef __EMSCRIPTEN__
    printf("WebGL2Renderer: Not running under Emscripten — cannot create GL context.\n");
    return false;
#else
    printf("WebGL2Renderer: Initializing...\n");

    // Compile the shared vertex shader once; each program links its own copy
    // (GL requires a separate compiled shader object per link, but we can
    // reuse the source text by compiling fresh instances cheaply).
    auto buildProg = [&](const char* fragSrc) -> GLuint {
        GLuint v = compileShader(GL_VERTEX_SHADER, kGL2VertexShader);
        if (!v) return 0;
        GLuint f = compileShader(GL_FRAGMENT_SHADER, fragSrc);
        return linkProgram(v, f);
    };

    mProgSolid     = buildProg(kGL2FragSolid);
    if (!mProgSolid) { printf("WebGL2Renderer: Failed to compile solid shader.\n"); return false; }

    mProgPixelText = buildProg(kGL2FragPixelText);
    if (!mProgPixelText) { printf("WebGL2Renderer: Failed to compile pixel-text shader.\n"); return false; }

    mProgKnob      = buildProg(kGL2FragKnob);
    mProgSlider    = buildProg(kGL2FragSlider);
    mProgCable     = buildProg(kGL2FragCable);
    mProgVU        = buildProg(kGL2FragVU);

    // Create VAO + VBO
    glGenVertexArrays(1, &mVAO);
    glGenBuffers(1, &mVBO);

    glBindVertexArray(mVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mVBO);

    // Stride = sizeof(GL2Vertex) = 8 floats = 32 bytes
    static_assert(sizeof(GL2Vertex) == 8 * sizeof(float), "GL2Vertex layout unexpected");
    GLsizei stride = sizeof(GL2Vertex);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GL2Vertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GL2Vertex, u));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GL2Vertex, r));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Font texture
    buildFontTexture();

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    printf("WebGL2Renderer: Initialization complete.\n");
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void WebGL2Renderer::beginFrame(int width, int height, float pixelRatio, float time)
{
    mWidth      = width;
    mHeight     = height;
    mPixelRatio = pixelRatio;
    mTime       = time;

    mVertices.clear();
    mPathPoints.clear();
    mPathHasStart = false;

#ifdef __EMSCRIPTEN__
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
#endif
}

void WebGL2Renderer::endFrame()
{
    // Nothing extra needed — GL renders immediately in flushVertices()
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void WebGL2Renderer::save()
{
    mStateStack.push_back(mCurrentState);
}

void WebGL2Renderer::restore()
{
    if (!mStateStack.empty()) {
        mCurrentState = mStateStack.back();
        mStateStack.pop_back();
    }
}

void WebGL2Renderer::reset()
{
    mCurrentState.transform[0] = 1.0f; mCurrentState.transform[1] = 0.0f;
    mCurrentState.transform[2] = 0.0f; mCurrentState.transform[3] = 1.0f;
    mCurrentState.transform[4] = 0.0f; mCurrentState.transform[5] = 0.0f;
    mCurrentState.fillColor   = Color(1.0f, 1.0f, 1.0f, 1.0f);
    mCurrentState.strokeColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
    mCurrentState.strokeWidth = 1.0f;
}

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------

void WebGL2Renderer::transformPoint(float& x, float& y) const
{
    const float* t = mCurrentState.transform;
    float nx = t[0] * x + t[2] * y + t[4];
    float ny = t[1] * x + t[3] * y + t[5];
    x = nx; y = ny;
}

void WebGL2Renderer::translate(float x, float y)
{
    mCurrentState.transform[4] += x;
    mCurrentState.transform[5] += y;
}

void WebGL2Renderer::rotate(float angle)
{
    float c = cosf(angle), s = sinf(angle);
    float* t = mCurrentState.transform;
    float a = t[0], b = t[1], cc = t[2], d = t[3];
    t[0] = a * c + cc * s;
    t[1] = b * c + d  * s;
    t[2] = a * -s + cc * c;
    t[3] = b * -s + d  * c;
}

void WebGL2Renderer::scale(float x, float y)
{
    mCurrentState.transform[0] *= x;
    mCurrentState.transform[1] *= x;
    mCurrentState.transform[2] *= y;
    mCurrentState.transform[3] *= y;
}

void WebGL2Renderer::resetTransform()
{
    mCurrentState.transform[0] = 1.0f; mCurrentState.transform[1] = 0.0f;
    mCurrentState.transform[2] = 0.0f; mCurrentState.transform[3] = 1.0f;
    mCurrentState.transform[4] = 0.0f; mCurrentState.transform[5] = 0.0f;
}

// ---------------------------------------------------------------------------
// Scissor
// ---------------------------------------------------------------------------

void WebGL2Renderer::scissor(float x, float y, float w, float h)
{
#ifdef __EMSCRIPTEN__
    glEnable(GL_SCISSOR_TEST);
    // Convert from top-left to bottom-left origin
    glScissor(static_cast<GLint>(x),
              static_cast<GLint>(mHeight - y - h),
              static_cast<GLsizei>(w),
              static_cast<GLsizei>(h));
#else
    (void)x; (void)y; (void)w; (void)h;
#endif
}

void WebGL2Renderer::resetScissor()
{
#ifdef __EMSCRIPTEN__
    glDisable(GL_SCISSOR_TEST);
#endif
}

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------

void WebGL2Renderer::fillColor(const Color& c)   { mCurrentState.fillColor   = c; }
void WebGL2Renderer::strokeColor(const Color& c) { mCurrentState.strokeColor = c; }
void WebGL2Renderer::strokeWidth(float w)        { mCurrentState.strokeWidth  = w; }

// ---------------------------------------------------------------------------
// Path
// ---------------------------------------------------------------------------

void WebGL2Renderer::beginPath()
{
    mPathPoints.clear();
    mPathHasStart = false;
}

void WebGL2Renderer::moveTo(float x, float y)
{
    mPathStartX = x; mPathStartY = y;
    mPathX = x; mPathY = y;
    mPathHasStart = true;
    mPathPoints.push_back(x);
    mPathPoints.push_back(y);
}

void WebGL2Renderer::lineTo(float x, float y)
{
    if (!mPathHasStart) moveTo(x, y);
    mPathX = x; mPathY = y;
    mPathPoints.push_back(x);
    mPathPoints.push_back(y);
}

void WebGL2Renderer::bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y)
{
    // Tessellate cubic bezier into line segments
    float px = mPathX, py = mPathY;
    const int steps = 12;
    for (int i = 1; i <= steps; i++) {
        float t  = static_cast<float>(i) / steps;
        float t2 = t * t, t3 = t2 * t;
        float mt = 1.0f - t, mt2 = mt * mt, mt3 = mt2 * mt;
        float bx = mt3*px + 3*mt2*t*c1x + 3*mt*t2*c2x + t3*x;
        float by = mt3*py + 3*mt2*t*c1y + 3*mt*t2*c2y + t3*y;
        lineTo(bx, by);
    }
}

void WebGL2Renderer::quadTo(float cx, float cy, float x, float y)
{
    float px = mPathX, py = mPathY;
    const int steps = 8;
    for (int i = 1; i <= steps; i++) {
        float t  = static_cast<float>(i) / steps;
        float mt = 1.0f - t;
        float bx = mt*mt*px + 2*mt*t*cx + t*t*x;
        float by = mt*mt*py + 2*mt*t*cy + t*t*y;
        lineTo(bx, by);
    }
}

void WebGL2Renderer::arc(float cx, float cy, float r, float a0, float a1, int dir)
{
    int steps = std::max(4, static_cast<int>(kArcSegments * r * 0.1f));
    float da  = (dir >= 0) ? (a1 - a0) : (a0 - a1);
    if (da < 0) da += 2.0f * kPI;
    if (dir < 0) da = -da;
    float step = da / steps;
    for (int i = 0; i <= steps; i++) {
        float a = a0 + step * i;
        float px = cx + cosf(a) * r;
        float py = cy + sinf(a) * r;
        if (i == 0) moveTo(px, py); else lineTo(px, py);
    }
}

void WebGL2Renderer::arcTo(float x1, float y1, float x2, float y2, float radius)
{
    // Simple approximation: arc from current point toward (x1,y1) then (x2,y2)
    lineTo(x1, y1);
    lineTo(x2, y2);
}

void WebGL2Renderer::closePath()
{
    if (mPathHasStart) lineTo(mPathStartX, mPathStartY);
}

// ---------------------------------------------------------------------------
// Internal vertex helpers
// ---------------------------------------------------------------------------

void WebGL2Renderer::flushVertices(GLuint program)
{
    if (mVertices.empty() || !program) return;
#ifdef __EMSCRIPTEN__
    glUseProgram(program);
    setUniformViewSize(program);

    glBindVertexArray(mVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mVertices.size() * sizeof(GL2Vertex)),
                 mVertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mVertices.size()));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
#endif
    mVertices.clear();
}

static void pushTriangleVerts(std::vector<WebGL2Renderer::GL2Vertex>& verts,
                               float x1, float y1,
                               float x2, float y2,
                               float x3, float y3,
                               float u1, float v1,
                               float u2, float v2,
                               float u3, float v3,
                               const Color& c)
{
    WebGL2Renderer::GL2Vertex vtx;
    vtx.r = c.r; vtx.g = c.g; vtx.b = c.b; vtx.a = c.a;

    vtx.x = x1; vtx.y = y1; vtx.u = u1; vtx.v = v1; verts.push_back(vtx);
    vtx.x = x2; vtx.y = y2; vtx.u = u2; vtx.v = v2; verts.push_back(vtx);
    vtx.x = x3; vtx.y = y3; vtx.u = u3; vtx.v = v3; verts.push_back(vtx);
}

void WebGL2Renderer::pushQuad(float x, float y, float w, float h, const Color& c)
{
    // Two triangles; UV (0,0)→(1,1)
    float x2 = x + w, y2 = y + h;
    float ax = x,  ay = y;
    float bx = x2, by = y;
    float cx = x2, cy = y2;
    float dx = x,  dy = y2;
    transformPoint(ax, ay);
    transformPoint(bx, by);
    transformPoint(cx, cy);
    transformPoint(dx, dy);

    GL2Vertex vtx;
    vtx.r = c.r; vtx.g = c.g; vtx.b = c.b; vtx.a = c.a;
    vtx.u = 0.0f; vtx.v = 0.0f;

    vtx.x = ax; vtx.y = ay; vtx.u = 0; vtx.v = 0; mVertices.push_back(vtx);
    vtx.x = bx; vtx.y = by; vtx.u = 1; vtx.v = 0; mVertices.push_back(vtx);
    vtx.x = cx; vtx.y = cy; vtx.u = 1; vtx.v = 1; mVertices.push_back(vtx);
    vtx.x = ax; vtx.y = ay; vtx.u = 0; vtx.v = 0; mVertices.push_back(vtx);
    vtx.x = cx; vtx.y = cy; vtx.u = 1; vtx.v = 1; mVertices.push_back(vtx);
    vtx.x = dx; vtx.y = dy; vtx.u = 0; vtx.v = 1; mVertices.push_back(vtx);
}

void WebGL2Renderer::pushFilledPath(const Color& c)
{
    if (mPathPoints.size() < 6) return; // need at least 3 points (2 values each)
    // Simple fan triangulation from first point
    size_t n = mPathPoints.size() / 2;
    float fx = mPathPoints[0], fy = mPathPoints[1];
    for (size_t i = 1; i + 1 < n; i++) {
        float ax = mPathPoints[i*2],     ay = mPathPoints[i*2+1];
        float bx = mPathPoints[(i+1)*2], by = mPathPoints[(i+1)*2+1];
        float tfx = fx, tfy = fy, tax = ax, tay = ay, tbx = bx, tby = by;
        transformPoint(tfx, tfy);
        transformPoint(tax, tay);
        transformPoint(tbx, tby);
        pushTriangleVerts(mVertices,
            tfx, tfy, tax, tay, tbx, tby,
            0,0, 0,0, 0,0, c);
    }
}

void WebGL2Renderer::pushStrokedPath(const Color& c, float width)
{
    if (mPathPoints.size() < 4) return;
    size_t n = mPathPoints.size() / 2;
    float hw = width * 0.5f;
    for (size_t i = 0; i + 1 < n; i++) {
        float x1 = mPathPoints[i*2],     y1 = mPathPoints[i*2+1];
        float x2 = mPathPoints[(i+1)*2], y2 = mPathPoints[(i+1)*2+1];
        float dx = x2 - x1, dy = y2 - y1;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 1e-6f) continue;
        float nx = -dy/len * hw, ny = dx/len * hw;

        float ax = x1+nx, ay = y1+ny;
        float bx = x1-nx, by = y1-ny;
        float cx2 = x2-nx, cy2 = y2-ny;
        float dx2 = x2+nx, dy2 = y2+ny;
        transformPoint(ax, ay);
        transformPoint(bx, by);
        transformPoint(cx2, cy2);
        transformPoint(dx2, dy2);

        pushTriangleVerts(mVertices, ax,ay, bx,by, cx2,cy2, 0,0.5f, 0,0.5f, 1,0.5f, c);
        pushTriangleVerts(mVertices, ax,ay, cx2,cy2, dx2,dy2, 0,0.5f, 1,0.5f, 1,0.5f, c);
    }
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

void WebGL2Renderer::fill()
{
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);
}

void WebGL2Renderer::stroke()
{
    pushStrokedPath(mCurrentState.strokeColor, mCurrentState.strokeWidth);
    flushVertices(mProgSolid);
}

void WebGL2Renderer::rect(float x, float y, float w, float h)
{
    beginPath();
    moveTo(x, y);
    lineTo(x+w, y);
    lineTo(x+w, y+h);
    lineTo(x, y+h);
    closePath();
}

void WebGL2Renderer::roundedRect(float x, float y, float w, float h, float r)
{
    r = std::min(r, std::min(w*0.5f, h*0.5f));
    beginPath();
    moveTo(x+r, y);
    arc(x+w-r, y+r,   r, -kPI*0.5f, 0.0f, 1);
    arc(x+w-r, y+h-r, r, 0.0f,      kPI*0.5f, 1);
    arc(x+r,   y+h-r, r, kPI*0.5f,  kPI,      1);
    arc(x+r,   y+r,   r, kPI,       kPI*1.5f, 1);
    closePath();
}

void WebGL2Renderer::circle(float cx, float cy, float r)
{
    beginPath();
    arc(cx, cy, r, 0.0f, 2.0f * kPI, 1);
    closePath();
}

void WebGL2Renderer::ellipse(float cx, float cy, float rx, float ry)
{
    beginPath();
    const int steps = kArcSegments;
    for (int i = 0; i <= steps; i++) {
        float a  = 2.0f * kPI * i / steps;
        float px = cx + cosf(a) * rx;
        float py = cy + sinf(a) * ry;
        if (i == 0) moveTo(px, py); else lineTo(px, py);
    }
    closePath();
}

void WebGL2Renderer::line(float x1, float y1, float x2, float y2)
{
    beginPath();
    moveTo(x1, y1);
    lineTo(x2, y2);
    pushStrokedPath(mCurrentState.strokeColor, mCurrentState.strokeWidth);
    flushVertices(mProgSolid);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

static const float kGL2CharWidthRatio = 0.6f;

void WebGL2Renderer::fontSize(float size) { mFontSize = size; }
void WebGL2Renderer::fontFace(const char*) {} // single built-in font

void WebGL2Renderer::renderGlyph(float x, float y, int charIdx, float charW, float charH,
                                  const Color& color)
{
    if (!mProgPixelText) return;

    // u0/u1 with a small inset to avoid float-precision boundary glitches
    float u0 = static_cast<float>(charIdx) + 0.02f;
    float u1 = static_cast<float>(charIdx) + 0.98f;

    float x1 = x, y1 = y - charH * 0.8f;
    float x2 = x + charW * 0.9f, y2 = y1 + charH * 0.9f;

    float ax = x1, ay = y1, bx = x2, by = y1, cx2 = x2, cy2 = y2, dx2 = x1, dy2 = y2;
    transformPoint(ax, ay);
    transformPoint(bx, by);
    transformPoint(cx2, cy2);
    transformPoint(dx2, dy2);

    GL2Vertex vtx;
    vtx.r = color.r; vtx.g = color.g; vtx.b = color.b; vtx.a = color.a;

    vtx.x = ax; vtx.y = ay; vtx.u = u0; vtx.v = 0.0f; mVertices.push_back(vtx);
    vtx.x = bx; vtx.y = by; vtx.u = u1; vtx.v = 0.0f; mVertices.push_back(vtx);
    vtx.x = cx2; vtx.y = cy2; vtx.u = u1; vtx.v = 1.0f; mVertices.push_back(vtx);
    vtx.x = ax; vtx.y = ay; vtx.u = u0; vtx.v = 0.0f; mVertices.push_back(vtx);
    vtx.x = cx2; vtx.y = cy2; vtx.u = u1; vtx.v = 1.0f; mVertices.push_back(vtx);
    vtx.x = dx2; vtx.y = dy2; vtx.u = u0; vtx.v = 1.0f; mVertices.push_back(vtx);
}

void WebGL2Renderer::text(float x, float y, const char* str)
{
    if (!str || str[0] == '\0') return;

    float charWidth   = mFontSize * kGL2CharWidthRatio;
    float charHeight  = mFontSize;
    float charSpacing = charWidth * 0.15f;
    Color c = mCurrentState.fillColor;

    float curX = x;
    for (size_t i = 0; str[i]; i++) {
        unsigned char ch = static_cast<unsigned char>(str[i]);

        // Determine glyph index; unknown or non-printable characters → skip (advance only)
        int charIdx = -1;
        if (ch >= 'a' && ch <= 'z') {
            charIdx = 59 + (ch - 'a');        // lowercase a-z → indices 59-84
        } else if (ch >= 32 && ch <= 90) {
            charIdx = ch - 32;                 // space + punctuation + digits + A-Z → 0-58
        }
        // ASCII 91-96: [, \, ], ^, _, ` — no glyph; advance only
        // ASCII 91-96 falls through to the advance below

        curX += charWidth + charSpacing;
        if (charIdx < 0) continue;
        // Render the glyph at the position before advancing (so we pass the pre-advance x)
        renderGlyph(curX - charWidth - charSpacing, y, charIdx, charWidth, charHeight, c);
    }

#ifdef __EMSCRIPTEN__
    // Bind font texture and flush
    if (!mVertices.empty() && mProgPixelText) {
        glUseProgram(mProgPixelText);
        setUniformViewSize(mProgPixelText);
        GLint tloc = glGetUniformLocation(mProgPixelText, "u_fontTex");
        if (tloc >= 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mFontTexture);
            glUniform1i(tloc, 0);
        }
        glBindVertexArray(mVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mVertices.size() * sizeof(GL2Vertex)),
                     mVertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mVertices.size()));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        mVertices.clear();
    }
#endif
}

float WebGL2Renderer::textWidth(const char* str)
{
    if (!str) return 0.0f;
    size_t len = strlen(str);
    float spacing = mFontSize * kGL2CharWidthRatio * 0.18f;
    return len * mFontSize * kGL2CharWidthRatio +
           (len > 0 ? (len - 1) * spacing : 0.0f);
}

// ---------------------------------------------------------------------------
// Specialised UI controls
// ---------------------------------------------------------------------------

void WebGL2Renderer::drawKnob(float cx, float cy, float radius, float value,
                               const Color& bgColor, const Color& fgColor)
{
    // Background disc
    fillColor(bgColor);
    circle(cx, cy, radius);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);

    // Highlight dome
    mVertices.clear();
    pushQuad(cx - radius, cy - radius, radius * 2, radius * 2, fgColor);
    flushVertices(mProgKnob ? mProgKnob : mProgSolid);

    // Indicator line
    float angle = -2.35619f + value * 4.71239f; // -135° to +135°
    float ix = cx + cosf(angle) * radius * 0.7f;
    float iy = cy + sinf(angle) * radius * 0.7f;
    strokeColor(Color(1.0f, 1.0f, 1.0f, 0.9f));
    strokeWidth(2.0f);
    line(cx, cy, ix, iy);
}

void WebGL2Renderer::drawWire(float x1, float y1, float x2, float y2,
                               const Color& color, float thickness)
{
    strokeColor(color);
    strokeWidth(thickness);
    beginPath();
    moveTo(x1, y1);
    lineTo(x2, y2);
    pushStrokedPath(color, thickness);
    flushVertices(mProgCable ? mProgCable : mProgSolid);
}

void WebGL2Renderer::drawCableWithSag(float x1, float y1, float x2, float y2,
                                       const Color& color, float thickness, float sag)
{
    // Catenary-style quadratic bezier
    float mx = (x1 + x2) * 0.5f;
    float my = (y1 + y2) * 0.5f + sag * fabsf(x2 - x1) * 0.5f;
    beginPath();
    moveTo(x1, y1);
    quadTo(mx, my, x2, y2);
    pushStrokedPath(color, thickness);
    flushVertices(mProgCable ? mProgCable : mProgSolid);
}

void WebGL2Renderer::drawSlider(float x, float y, float w, float h, float value,
                                 const Color& bgColor, const Color& fgColor)
{
    // Track background
    fillColor(bgColor);
    roundedRect(x, y, w, h, h * 0.5f);
    pushFilledPath(bgColor);
    flushVertices(mProgSolid);

    // Fill up to value
    float fw = w * std::max(0.0f, std::min(1.0f, value));
    if (fw > h) {
        fillColor(fgColor);
        roundedRect(x, y, fw, h, h * 0.5f);
        pushFilledPath(fgColor);
        flushVertices(mProgSlider ? mProgSlider : mProgSolid);
    }

    // Handle
    float hx = x + fw - h * 0.5f;
    fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
    circle(hx + h * 0.5f, y + h * 0.5f, h * 0.45f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);
}

void WebGL2Renderer::drawVUMeter(float x, float y, float w, float h, float level,
                                  const Color& lowColor, const Color& highColor)
{
    // Background
    fillColor(Color(0.15f, 0.15f, 0.17f, 1.0f));
    roundedRect(x, y, w, h, 2.0f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);

    // Level segments
    int numSegs = 14;
    float segH = h / numSegs - 1.0f;
    int activeSeg = static_cast<int>(level * numSegs);
    for (int i = 0; i < numSegs; i++) {
        float sy = y + h - (i + 1) * (h / numSegs);
        Color segColor = (i >= static_cast<int>(numSegs * 0.7f)) ? highColor : lowColor;
        segColor.a = (i < activeSeg) ? 1.0f : 0.2f;
        fillColor(segColor);
        roundedRect(x + 1, sy + 1, w - 2, segH, 1.0f);
        pushFilledPath(mCurrentState.fillColor);
        flushVertices(mProgVU ? mProgVU : mProgSolid);
    }
}

void WebGL2Renderer::drawButton(float x, float y, float w, float h,
                                 const char* label, bool pressed, bool /*hover*/)
{
    Color bg = pressed ? Color(0.4f, 0.5f, 0.7f, 1.0f) : Color(0.25f, 0.25f, 0.3f, 1.0f);
    fillColor(bg);
    roundedRect(x, y, w, h, 4.0f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);

    if (label && label[0]) {
        float tw = textWidth(label);
        fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
        text(x + (w - tw) * 0.5f, y + h * 0.65f, label);
    }
}

void WebGL2Renderer::drawToggle(float x, float y, float w, float h, bool state)
{
    Color bg = state ? Color(0.3f, 0.7f, 0.5f, 1.0f) : Color(0.3f, 0.3f, 0.35f, 1.0f);
    fillColor(bg);
    roundedRect(x, y, w, h, h * 0.5f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);

    float tx = state ? x + w - h + 2 : x + 2;
    fillColor(Color(0.95f, 0.95f, 0.95f, 1.0f));
    circle(tx + h * 0.4f, y + h * 0.5f, h * 0.4f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);
}

void WebGL2Renderer::drawFader(float x, float y, float w, float h, float value)
{
    fillColor(Color(0.2f, 0.2f, 0.22f, 1.0f));
    roundedRect(x + w*0.4f, y, w*0.2f, h, w*0.1f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);

    float capY = y + h * (1.0f - value) - 8.0f;
    fillColor(Color(0.7f, 0.7f, 0.75f, 1.0f));
    roundedRect(x, capY, w, 16.0f, 3.0f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);
}

void WebGL2Renderer::drawADSR(float x, float y, float w, float h,
                               float a, float d, float s, float r)
{
    // Draw grid
    strokeColor(Color(0.3f, 0.3f, 0.35f, 0.5f));
    strokeWidth(0.5f);
    for (int i = 1; i < 4; i++) {
        line(x + w * i * 0.25f, y, x + w * i * 0.25f, y + h);
    }

    // Envelope shape
    float aw = w * 0.2f * a, dw = w * 0.2f * d;
    float sw = w * 0.4f, rw = w * 0.2f * r;
    float top = y + 4, bot = y + h - 4;
    float sl = bot + (top - bot) * s;

    strokeColor(Color(0.3f, 0.8f, 0.5f, 0.9f));
    strokeWidth(2.0f);
    beginPath();
    moveTo(x, bot);
    lineTo(x + aw, top);
    lineTo(x + aw + dw, sl);
    lineTo(x + aw + dw + sw, sl);
    lineTo(x + aw + dw + sw + rw, bot);
    pushStrokedPath(mCurrentState.strokeColor, mCurrentState.strokeWidth);
    flushVertices(mProgSolid);
}

void WebGL2Renderer::drawPanel(float x, float y, float w, float h, bool bordered)
{
    fillColor(Color(0.18f, 0.18f, 0.2f, 0.95f));
    roundedRect(x, y, w, h, 6.0f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);

    if (bordered) {
        strokeColor(Color(0.35f, 0.35f, 0.4f, 1.0f));
        strokeWidth(1.0f);
        roundedRect(x, y, w, h, 6.0f);
        pushStrokedPath(mCurrentState.strokeColor, mCurrentState.strokeWidth);
        flushVertices(mProgSolid);
    }
}

void WebGL2Renderer::drawLED(float x, float y, float w, float h, bool on)
{
    Color col = on ? Color(0.2f, 0.9f, 0.3f, 1.0f) : Color(0.2f, 0.3f, 0.2f, 1.0f);
    fillColor(col);
    circle(x + w * 0.5f, y + h * 0.5f, std::min(w, h) * 0.45f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);
}

void WebGL2Renderer::drawProgressBar(float x, float y, float w, float h, float value)
{
    fillColor(Color(0.2f, 0.2f, 0.22f, 1.0f));
    roundedRect(x, y, w, h, h * 0.5f);
    pushFilledPath(mCurrentState.fillColor);
    flushVertices(mProgSolid);

    float fw = w * std::max(0.0f, std::min(1.0f, value));
    if (fw > 1.0f) {
        fillColor(Color(0.3f, 0.7f, 0.9f, 1.0f));
        roundedRect(x, y, fw, h, h * 0.5f);
        pushFilledPath(mCurrentState.fillColor);
        flushVertices(mProgSolid);
    }
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

uint8_t* WebGL2Renderer::captureFrame(int& outWidth, int& outHeight)
{
    outWidth  = mWidth;
    outHeight = mHeight;
#ifdef __EMSCRIPTEN__
    int bytes = mWidth * mHeight * 4;
    if (bytes <= 0) return nullptr;
    uint8_t* pixels = new uint8_t[bytes];
    glReadPixels(0, 0, mWidth, mHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    // WebGL/GL readPixels gives bottom-to-top rows; flip vertically
    std::vector<uint8_t> row(static_cast<size_t>(mWidth) * 4);
    for (int i = 0; i < mHeight / 2; i++) {
        int j = mHeight - 1 - i;
        memcpy(row.data(), pixels + i * mWidth * 4, mWidth * 4);
        memcpy(pixels + i * mWidth * 4, pixels + j * mWidth * 4, mWidth * 4);
        memcpy(pixels + j * mWidth * 4, row.data(), mWidth * 4);
    }
    return pixels;
#else
    return nullptr;
#endif
}

} // namespace wasm
} // namespace bespoke
