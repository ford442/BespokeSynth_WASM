/**
 * BespokeSynth WASM - Shared 5x7 pixel font implementation
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/PixelFont.h"
#include <cstring>

namespace bespoke {
namespace wasm {

namespace {

const uint32_t kFontCols[kPixelFontGlyphCount * kPixelFontColumnsPerGlyph] = {
#include "BespokeWasm/pixel_font_glyphs.inc"
};

} // namespace

const uint32_t* getPixelFontColumns()
{
   return kFontCols;
}

int pixelFontCharIndex(unsigned char c)
{
   if (c >= 32 && c <= 126)
      return static_cast<int>(c) - 32;
   return -1;
}

PixelFontGlyph pixelFontDecodeGlyph(const char* string, size_t offset, size_t totalLen)
{
   PixelFontGlyph glyph{ 0, 1 };
   if (!string || offset >= totalLen)
   {
      glyph.byteLength = 0;
      return glyph;
   }

   const unsigned char c0 = static_cast<unsigned char>(string[offset]);
   const int asciiIndex = pixelFontCharIndex(c0);
   if (asciiIndex >= 0)
   {
      glyph.index = asciiIndex;
      glyph.byteLength = 1;
      return glyph;
   }

   if (totalLen - offset >= 3 && c0 == 0xE2 && static_cast<unsigned char>(string[offset + 1]) == 0x99)
   {
      const unsigned char c2 = static_cast<unsigned char>(string[offset + 2]);
      if (c2 == 0xAF)
      {
         glyph.index = kPixelFontGlyphSharp;
         glyph.byteLength = 3;
         return glyph;
      }
      if (c2 == 0xAD)
      {
         glyph.index = kPixelFontGlyphFlat;
         glyph.byteLength = 3;
         return glyph;
      }
   }

   return glyph;
}

float pixelFontGlyphAdvance(int glyphIndex, float fontSize)
{
   if (glyphIndex < 0 || glyphIndex >= kPixelFontGlyphCount)
      return 0.0f;

   if (glyphIndex == 0)
      return fontSize * kPixelFontSpaceWidthRatio;

   return fontSize * kPixelFontCharWidthRatio;
}

float pixelFontTextWidth(const char* string, float fontSize)
{
   if (!string)
      return 0.0f;

   const size_t len = strlen(string);
   if (len == 0)
      return 0.0f;

   const float spacing = fontSize * kPixelFontCharSpacingRatio;
   float width = 0.0f;
   bool hasGlyph = false;

   for (size_t i = 0; i < len;)
   {
      const PixelFontGlyph glyph = pixelFontDecodeGlyph(string, i, len);
      if (glyph.byteLength == 0)
         break;

      const float advance = pixelFontGlyphAdvance(glyph.index, fontSize);
      if (advance > 0.0f)
      {
         if (hasGlyph)
            width += spacing;
         width += advance;
         hasGlyph = true;
      }

      i += glyph.byteLength;
   }

   return width;
}

float pixelFontTextHeight(float fontSize)
{
   return fontSize;
}

} // namespace wasm
} // namespace bespoke
