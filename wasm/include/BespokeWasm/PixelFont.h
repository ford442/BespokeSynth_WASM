/**
 * BespokeSynth WASM - Shared 5x7 pixel font
 * Used by WebGPU and WebGL2 text renderers
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace bespoke {
namespace wasm {

constexpr int kPixelFontGlyphCount = 95; // ASCII 32-126
constexpr int kPixelFontColumnsPerGlyph = 5;
constexpr int kPixelFontRows = 7;
constexpr float kPixelFontCharWidthRatio = 0.62f;
constexpr float kPixelFontCharSpacingRatio = 0.12f;
constexpr float kPixelFontBaselineRatio = 0.78f;

const uint32_t* getPixelFontColumns();
int pixelFontCharIndex(unsigned char c);
float pixelFontTextWidth(const char* string, float fontSize);
float pixelFontTextHeight(float fontSize);

} // namespace wasm
} // namespace bespoke
