/**
 * BespokeSynth WASM - Shared 5x7 pixel font implementation
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "PixelFont.h"
#include <cstring>

namespace bespoke {
namespace wasm {

namespace {

const uint32_t kFontCols[kPixelFontGlyphCount * kPixelFontColumnsPerGlyph] = {
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
   16u, 42u, 42u, 42u, 28u,
   127u, 66u, 66u, 66u, 60u,
   60u, 66u, 66u, 66u, 36u,
   60u, 66u, 66u, 66u, 127u,
   60u, 74u, 74u, 74u, 60u,
   4u, 126u, 5u, 1u, 2u,
   44u, 74u, 74u, 74u, 60u,
   127u, 8u, 4u, 4u, 120u,
   0u, 68u, 125u, 64u, 0u,
   32u, 64u, 64u, 63u, 0u,
   127u, 16u, 40u, 68u, 0u,
   0u, 65u, 127u, 64u, 0u,
   124u, 4u, 4u, 4u, 120u,
   124u, 4u, 4u, 4u, 120u,
   56u, 68u, 68u, 68u, 56u,
   124u, 20u, 20u, 20u, 8u,
   8u, 20u, 20u, 20u, 124u,
   124u, 4u, 4u, 4u, 0u,
   72u, 84u, 84u, 84u, 48u,
   4u, 63u, 68u, 68u, 32u,
   56u, 64u, 64u, 64u, 56u,
   28u, 32u, 64u, 32u, 28u,
   56u, 64u, 48u, 64u, 56u,
   76u, 16u, 8u, 16u, 76u,
   28u, 32u, 32u, 32u, 124u,
   68u, 84u, 84u, 84u, 36u,
   0u, 8u, 54u, 65u, 0u,
   0u, 0u, 127u, 0u, 0u,
   0u, 65u, 54u, 8u, 0u,
   8u, 20u, 8u, 20u, 8u,
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
   return 0;
}

float pixelFontTextWidth(const char* string, float fontSize)
{
   if (!string)
      return 0.0f;

   const size_t len = strlen(string);
   if (len == 0)
      return 0.0f;

   const float charWidth = fontSize * kPixelFontCharWidthRatio;
   const float spacing = fontSize * kPixelFontCharSpacingRatio;
   return static_cast<float>(len) * charWidth + static_cast<float>(len - 1) * spacing;
}

float pixelFontTextHeight(float fontSize)
{
   return fontSize;
}

} // namespace wasm
} // namespace bespoke
