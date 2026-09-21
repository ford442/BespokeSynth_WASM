/**
 * BespokeSynth WASM - SDF font atlas loader and text layout
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/SdfFont.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {

      namespace
      {

         // Layout of ui_font.bsdf (little-endian). Keep in sync with bake_sdf_font.cpp.
         constexpr char kMagic[4] = { 'B', 'S', 'D', 'F' };
         constexpr uint32_t kVersion = 1;
         constexpr size_t kHeaderBytes = 4 + 4 + 2 + 2 + 5 * 4 + 4 + 4;
         constexpr size_t kGlyphRecordBytes = 4 + 5 * 4 + 4 * 2;
         constexpr size_t kKernRecordBytes = 4 + 4 + 4;

         class Reader
         {
         public:
            Reader(const uint8_t* data, size_t size)
            : mData(data)
            , mSize(size)
            {
            }

            template <typename T>
            T read()
            {
               T value{};
               if (mOffset + sizeof(T) <= mSize)
                  std::memcpy(&value, mData + mOffset, sizeof(T));
               mOffset += sizeof(T);
               return value;
            }

            size_t offset() const { return mOffset; }

         private:
            const uint8_t* mData;
            size_t mSize;
            size_t mOffset = 0;
         };

         UiFontMode gUiFontMode = UiFontMode::Sdf;

      } // namespace

      size_t decodeUtf8(const char* string, size_t offset, size_t length, uint32_t& codepoint)
      {
         if (!string || offset >= length)
            return 0;

         const auto byteAt = [&](size_t i)
         {
            return static_cast<unsigned char>(string[offset + i]);
         };
         const unsigned char c0 = byteAt(0);
         size_t count = 1;
         uint32_t cp = c0;
         if (c0 >= 0xF8)
         {
            codepoint = 0xFFFD; // not a valid UTF-8 lead byte
            return 1;
         }
         if (c0 >= 0xF0)
         {
            count = 4;
            cp = c0 & 0x07u;
         }
         else if (c0 >= 0xE0)
         {
            count = 3;
            cp = c0 & 0x0Fu;
         }
         else if (c0 >= 0xC0)
         {
            count = 2;
            cp = c0 & 0x1Fu;
         }
         else if (c0 >= 0x80)
         {
            codepoint = 0xFFFD; // stray continuation byte
            return 1;
         }

         if (offset + count > length)
         {
            codepoint = 0xFFFD;
            return 1;
         }
         for (size_t i = 1; i < count; ++i)
         {
            const unsigned char c = byteAt(i);
            if ((c & 0xC0u) != 0x80u)
            {
               codepoint = 0xFFFD;
               return 1;
            }
            cp = (cp << 6) | (c & 0x3Fu);
         }
         codepoint = cp;
         return count;
      }

      bool SdfFont::loadFromMemory(const uint8_t* data, size_t size)
      {
         clear();
         if (!data || size < kHeaderBytes || std::memcmp(data, kMagic, sizeof(kMagic)) != 0)
            return false;

         Reader in(data, size);
         in.read<uint32_t>(); // magic
         if (in.read<uint32_t>() != kVersion)
            return false;

         const int width = in.read<uint16_t>();
         const int height = in.read<uint16_t>();
         in.read<float>(); // baked em size in texels (informational)
         const float pxRange = in.read<float>();
         in.read<float>(); // ascender (em)
         in.read<float>(); // descender (em)
         in.read<float>(); // line gap (em)
         const uint32_t glyphCount = in.read<uint32_t>();
         const uint32_t kernCount = in.read<uint32_t>();

         const size_t pixelBytes = static_cast<size_t>(width) * static_cast<size_t>(height);
         const size_t expected = kHeaderBytes + glyphCount * kGlyphRecordBytes + kernCount * kKernRecordBytes + pixelBytes;
         if (width <= 0 || height <= 0 || pxRange <= 0.0f || expected != size)
            return false;

         mGlyphs.reserve(glyphCount);
         for (uint32_t i = 0; i < glyphCount; ++i)
         {
            SdfGlyph g;
            g.codepoint = in.read<uint32_t>();
            g.advance = in.read<float>();
            g.left = in.read<float>();
            g.top = in.read<float>();
            g.right = in.read<float>();
            g.bottom = in.read<float>();
            const float ax = in.read<uint16_t>();
            const float ay = in.read<uint16_t>();
            const float aw = in.read<uint16_t>();
            const float ah = in.read<uint16_t>();
            g.hasBitmap = aw > 0.0f && ah > 0.0f;
            g.u0 = ax / static_cast<float>(width);
            g.v0 = ay / static_cast<float>(height);
            g.u1 = (ax + aw) / static_cast<float>(width);
            g.v1 = (ay + ah) / static_cast<float>(height);
            mGlyphs.push_back(g);
         }
         std::sort(mGlyphs.begin(), mGlyphs.end(),
                   [](const SdfGlyph& a, const SdfGlyph& b)
                   {
                      return a.codepoint < b.codepoint;
                   });

         mKerns.reserve(kernCount);
         for (uint32_t i = 0; i < kernCount; ++i)
         {
            Kern k;
            k.left = in.read<uint32_t>();
            k.right = in.read<uint32_t>();
            k.amount = in.read<float>();
            mKerns.push_back(k);
         }
         std::sort(mKerns.begin(), mKerns.end(), [](const Kern& a, const Kern& b)
                   {
                      return a.left != b.left ? a.left < b.left : a.right < b.right;
                   });

         mPixels.assign(data + in.offset(), data + in.offset() + pixelBytes);
         mAtlasWidth = width;
         mAtlasHeight = height;
         mPxRange = pxRange;
         if (const SdfGlyph* space = findGlyph(' '))
            mSpaceAdvance = space->advance;
         mLoaded = true;
         return true;
      }

      bool SdfFont::loadFromFile(const char* path)
      {
         clear();
         FILE* fp = path ? std::fopen(path, "rb") : nullptr;
         if (!fp)
            return false;
         std::fseek(fp, 0, SEEK_END);
         const long size = std::ftell(fp);
         std::fseek(fp, 0, SEEK_SET);
         std::vector<uint8_t> bytes(size > 0 ? static_cast<size_t>(size) : 0);
         const size_t read = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), fp);
         std::fclose(fp);
         return read == bytes.size() && loadFromMemory(bytes.data(), bytes.size());
      }

      void SdfFont::clear()
      {
         mLoaded = false;
         mAtlasWidth = 0;
         mAtlasHeight = 0;
         mPxRange = 0.0f;
         mSpaceAdvance = 0.25f;
         mGlyphs.clear();
         mKerns.clear();
         mPixels.clear();
      }

      const SdfGlyph* SdfFont::findGlyph(uint32_t codepoint) const
      {
         const auto it = std::lower_bound(mGlyphs.begin(), mGlyphs.end(), codepoint,
                                          [](const SdfGlyph& g, uint32_t cp)
                                          {
                                             return g.codepoint < cp;
                                          });
         return it != mGlyphs.end() && it->codepoint == codepoint ? &*it : nullptr;
      }

      float SdfFont::kerning(uint32_t left, uint32_t right) const
      {
         const auto it = std::lower_bound(mKerns.begin(), mKerns.end(), Kern{ left, right, 0.0f },
                                          [](const Kern& a, const Kern& b)
                                          {
                                             return a.left != b.left ? a.left < b.left : a.right < b.right;
                                          });
         return it != mKerns.end() && it->left == left && it->right == right ? it->amount : 0.0f;
      }

      float SdfFont::advanceFor(const SdfGlyph* glyph) const
      {
         // Unknown codepoints advance like a space, as the pixel font does.
         return glyph ? glyph->advance : mSpaceAdvance;
      }

      float SdfFont::textWidth(const char* string, float fontSize) const
      {
         if (!string || !mLoaded)
            return 0.0f;

         const float em = fontSize * kSdfFontEmScale;
         const size_t length = std::strlen(string);
         float width = 0.0f;
         uint32_t previous = 0;
         for (size_t i = 0; i < length;)
         {
            uint32_t codepoint = 0;
            const size_t consumed = decodeUtf8(string, i, length, codepoint);
            if (consumed == 0)
               break;
            i += consumed;

            const SdfGlyph* glyph = findGlyph(codepoint);
            if (previous != 0 && glyph)
               width += kerning(previous, codepoint) * em;
            width += advanceFor(glyph) * em;
            previous = glyph ? codepoint : 0;
         }
         return width;
      }

      const SdfFont& uiSdfFont()
      {
         static SdfFont font;
         static bool attempted = false;
         if (!attempted)
         {
            attempted = true;
            if (font.loadFromFile(kUiSdfFontPath))
               std::printf("SdfFont: loaded %s (%zu glyphs, %dx%d atlas, %zu bytes)\n", kUiSdfFontPath, font.glyphCount(),
                           font.atlasWidth(), font.atlasHeight(), font.atlasBytes());
            else
               std::printf("SdfFont: %s unavailable; using the 5x7 pixel font\n", kUiSdfFontPath);
         }
         return font;
      }

      void setUiFontMode(UiFontMode mode)
      {
         gUiFontMode = mode;
      }

      UiFontMode uiFontMode()
      {
         return gUiFontMode;
      }

      bool useSdfTextForFace(const char* faceName)
      {
         if (gUiFontMode == UiFontMode::Pixel)
            return false;
         if (faceName && std::strcmp(faceName, kPixelFontFaceName) == 0)
            return false;
         return uiSdfFont().isLoaded();
      }

   } // namespace wasm
} // namespace bespoke
