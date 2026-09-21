/**
 * BespokeSynth WASM - SDF font atlas (shared by WebGPU and WebGL2 text)
 *
 * The atlas is baked offline by wasm/tools/sdf_font_baker (scripts/bake_sdf_font.sh)
 * into wasm/resource-pack/fonts/ui_font.bsdf. Both backends upload its pixels as
 * a single R8 texture and draw glyph quads laid out here, so textWidth() and the
 * pixels on screen always agree.
 *
 * Coordinates follow the 5x7 pixel font's conventions (y is the baseline,
 * textHeight() == fontSize) so layouts keyed to kPixelFontBaselineRatio keep
 * lining up. The 5x7 font remains available via ?debugFont=pixel and the
 * fontFace("pixel") face used by the font regression panel.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      constexpr const char* kUiSdfFontPath = "/resource/fonts/ui_font.bsdf";

      // Glyph em size relative to fontSize: matches the SDF font's cap height to the
      // 5x7 font's (5 of 7 rows), so mixed layouts and baselines stay aligned.
      constexpr float kSdfFontEmScale = 1.07f;

      // Face name that forces the 5x7 bitmap font for the next text() calls.
      constexpr const char* kPixelFontFaceName = "pixel";

      enum class UiFontMode
      {
         Sdf = 0, ///< SDF atlas when it loaded; falls back to the pixel font otherwise
         Pixel = 1, ///< Always use the 5x7 pixel font (?debugFont=pixel)
      };

      struct SdfGlyph
      {
         uint32_t codepoint = 0;
         float advance = 0.0f; ///< em
         // Quad bounds in em, relative to the pen position (y down from the baseline).
         float left = 0.0f;
         float top = 0.0f;
         float right = 0.0f;
         float bottom = 0.0f;
         // Atlas rect in normalized UV.
         float u0 = 0.0f;
         float v0 = 0.0f;
         float u1 = 0.0f;
         float v1 = 0.0f;
         bool hasBitmap = false;
      };

      struct SdfGlyphQuad
      {
         float x0, y0, x1, y1; ///< screen space before the renderer transform
         float u0, v0, u1, v1;
      };

      class SdfFont
      {
      public:
         bool loadFromMemory(const uint8_t* data, size_t size);
         bool loadFromFile(const char* path);
         void clear();

         bool isLoaded() const { return mLoaded; }
         int atlasWidth() const { return mAtlasWidth; }
         int atlasHeight() const { return mAtlasHeight; }
         const uint8_t* atlasPixels() const { return mPixels.data(); }
         size_t atlasBytes() const { return mPixels.size(); }
         /// Atlas texels spanned by the stored distance range (d = 0..1).
         float pxRange() const { return mPxRange; }
         size_t glyphCount() const { return mGlyphs.size(); }

         const SdfGlyph* findGlyph(uint32_t codepoint) const;
         /// Extra advance between two glyphs, in em.
         float kerning(uint32_t left, uint32_t right) const;

         float textWidth(const char* string, float fontSize) const;

         /// Lays out UTF-8 text starting at the pen (x, baselineY). Calls emit(const SdfGlyphQuad&)
         /// for every glyph with a bitmap; whitespace and unknown codepoints only advance.
         template <typename EmitQuad>
         void layout(const char* string, float x, float baselineY, float fontSize, EmitQuad&& emit) const;

      private:
         struct Kern
         {
            uint32_t left;
            uint32_t right;
            float amount;
         };

         float advanceFor(const SdfGlyph* glyph) const;

         bool mLoaded = false;
         int mAtlasWidth = 0;
         int mAtlasHeight = 0;
         float mPxRange = 0.0f;
         float mSpaceAdvance = 0.25f;
         std::vector<SdfGlyph> mGlyphs; ///< sorted by codepoint
         std::vector<Kern> mKerns; ///< sorted by (left, right)
         std::vector<uint8_t> mPixels;
      };

      /// Decodes one UTF-8 codepoint; returns bytes consumed (>= 1 while bytes remain, 0 at end).
      size_t decodeUtf8(const char* string, size_t offset, size_t length, uint32_t& codepoint);

      /// Process-wide UI font; loads kUiSdfFontPath on first use.
      const SdfFont& uiSdfFont();

      void setUiFontMode(UiFontMode mode);
      UiFontMode uiFontMode();

      /// True when text drawn with this face should use the SDF atlas.
      bool useSdfTextForFace(const char* faceName);

      template <typename EmitQuad>
      void SdfFont::layout(const char* string, float x, float baselineY, float fontSize, EmitQuad&& emit) const
      {
         if (!string || !mLoaded)
            return;

         const float em = fontSize * kSdfFontEmScale;
         float penX = x;
         uint32_t previous = 0;
         size_t length = 0;
         while (string[length] != '\0')
            ++length;

         for (size_t i = 0; i < length;)
         {
            uint32_t codepoint = 0;
            const size_t consumed = decodeUtf8(string, i, length, codepoint);
            if (consumed == 0)
               break;
            i += consumed;

            const SdfGlyph* glyph = findGlyph(codepoint);
            if (previous != 0 && glyph)
               penX += kerning(previous, codepoint) * em;

            if (glyph && glyph->hasBitmap)
            {
               SdfGlyphQuad quad;
               quad.x0 = penX + glyph->left * em;
               quad.x1 = penX + glyph->right * em;
               quad.y0 = baselineY + glyph->top * em;
               quad.y1 = baselineY + glyph->bottom * em;
               quad.u0 = glyph->u0;
               quad.v0 = glyph->v0;
               quad.u1 = glyph->u1;
               quad.v1 = glyph->v1;
               emit(quad);
            }

            penX += advanceFor(glyph) * em;
            previous = glyph ? codepoint : 0;
         }
      }

   } // namespace wasm
} // namespace bespoke
