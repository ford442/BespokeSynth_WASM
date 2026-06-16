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

constexpr int kPixelFontAsciiGlyphCount = 95; // ASCII 32-126
constexpr int kPixelFontExtendedGlyphCount = 6; // sharp, flat, arrows
constexpr int kPixelFontGlyphCount = kPixelFontAsciiGlyphCount + kPixelFontExtendedGlyphCount;
constexpr int kPixelFontColumnsPerGlyph = 5;
constexpr int kPixelFontRows = 7;

constexpr int kPixelFontGlyphSharp = kPixelFontAsciiGlyphCount + 0;
constexpr int kPixelFontGlyphFlat = kPixelFontAsciiGlyphCount + 1;
constexpr int kPixelFontGlyphArrowUp = kPixelFontAsciiGlyphCount + 2;
constexpr int kPixelFontGlyphArrowDown = kPixelFontAsciiGlyphCount + 3;
constexpr int kPixelFontGlyphArrowLeft = kPixelFontAsciiGlyphCount + 4;
constexpr int kPixelFontGlyphArrowRight = kPixelFontAsciiGlyphCount + 5;

constexpr float kPixelFontCharWidthRatio = 0.58f;
constexpr float kPixelFontCharSpacingRatio = 0.10f;
constexpr float kPixelFontSpaceWidthRatio = 0.35f;
constexpr float kPixelFontBaselineRatio = 0.75f;

struct PixelFontGlyph
{
   int index;
   size_t byteLength;
};

const uint32_t* getPixelFontColumns();
int pixelFontCharIndex(unsigned char c);
PixelFontGlyph pixelFontDecodeGlyph(const char* string, size_t offset, size_t totalLen);
float pixelFontGlyphAdvance(int glyphIndex, float fontSize);
float pixelFontTextWidth(const char* string, float fontSize);
float pixelFontTextHeight(float fontSize);

} // namespace wasm
} // namespace bespoke
