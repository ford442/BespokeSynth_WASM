/**
 * BespokeSynth WASM - SDF font atlas baker (host tool, not part of the .wasm)
 *
 * Bakes a single-channel signed distance field atlas plus glyph metrics from a
 * TrueType font using the stb_truetype copy vendored with nanovg. The output is
 * the .bsdf blob read by SdfFont (wasm/src/SdfFont.cpp); both WebGPU and WebGL2
 * upload its pixels as one R8 texture.
 *
 * Build + run through scripts/bake_sdf_font.sh. Usage:
 *   bake_sdf_font --font primary.ttf [--fallback other.ttf] --out atlas.bsdf
 *                 [--em 32] [--spread 4] [--width 512]
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
   // Keep in sync with SdfFont.cpp.
   constexpr char kMagic[4] = { 'B', 'S', 'D', 'F' };
   constexpr uint32_t kVersion = 1;
   constexpr int kGutter = 1; // empty texels between packed glyphs

   struct Font
   {
      std::vector<unsigned char> data;
      stbtt_fontinfo info{};
      float scale = 0.0f; // font units -> atlas texels for one em
   };

   struct Glyph
   {
      uint32_t codepoint = 0;
      const Font* font = nullptr;
      float advance = 0.0f; // em
      float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f; // em, y down from baseline
      int w = 0, h = 0;
      int x = 0, y = 0; // atlas placement
      std::vector<unsigned char> pixels;
   };

   struct Kern
   {
      uint32_t left, right;
      float amount; // em
   };

   bool loadFont(const std::string& path, float emPx, Font& out)
   {
      FILE* fp = std::fopen(path.c_str(), "rb");
      if (!fp)
      {
         std::fprintf(stderr, "bake_sdf_font: cannot open %s\n", path.c_str());
         return false;
      }
      std::fseek(fp, 0, SEEK_END);
      const long size = std::ftell(fp);
      std::fseek(fp, 0, SEEK_SET);
      out.data.resize(static_cast<size_t>(size));
      const size_t read = std::fread(out.data.data(), 1, out.data.size(), fp);
      std::fclose(fp);
      if (read != out.data.size() || !stbtt_InitFont(&out.info, out.data.data(), 0))
      {
         std::fprintf(stderr, "bake_sdf_font: %s is not a TrueType font\n", path.c_str());
         return false;
      }
      out.scale = stbtt_ScaleForMappingEmToPixels(&out.info, emPx);
      return true;
   }

   // ASCII plus the handful of symbols the UI prints (degrees, micro, times,
   // en dash, bullet, ellipsis, arrows, flat, sharp).
   std::vector<uint32_t> charset()
   {
      std::vector<uint32_t> cps;
      for (uint32_t c = 32; c <= 126; ++c)
         cps.push_back(c);
      const uint32_t extras[] = { 0x00B0, 0x00B5, 0x00D7, 0x2013, 0x2022, 0x2026,
                                  0x2190, 0x2191, 0x2192, 0x2193, 0x266D, 0x266F };
      cps.insert(cps.end(), std::begin(extras), std::end(extras));
      return cps;
   }

   template <typename T>
   void put(std::vector<unsigned char>& out, T value)
   {
      unsigned char bytes[sizeof(T)];
      std::memcpy(bytes, &value, sizeof(T)); // host is little-endian (x86/arm)
      out.insert(out.end(), bytes, bytes + sizeof(T));
   }
}

int main(int argc, char** argv)
{
   std::string fontPath;
   std::string fallbackPath;
   std::string outPath;
   float emPx = 32.0f;
   int spread = 4;
   int atlasWidth = 512;

   for (int i = 1; i < argc; ++i)
   {
      const std::string arg = argv[i];
      const bool hasValue = i + 1 < argc;
      if (arg == "--font" && hasValue)
         fontPath = argv[++i];
      else if (arg == "--fallback" && hasValue)
         fallbackPath = argv[++i];
      else if (arg == "--out" && hasValue)
         outPath = argv[++i];
      else if (arg == "--em" && hasValue)
         emPx = static_cast<float>(std::atof(argv[++i]));
      else if (arg == "--spread" && hasValue)
         spread = std::atoi(argv[++i]);
      else if (arg == "--width" && hasValue)
         atlasWidth = std::atoi(argv[++i]);
      else
      {
         std::fprintf(stderr, "bake_sdf_font: unknown argument %s\n", arg.c_str());
         return 2;
      }
   }
   if (fontPath.empty() || outPath.empty() || emPx <= 0.0f || spread <= 0 || atlasWidth <= 0)
   {
      std::fprintf(stderr,
                   "usage: bake_sdf_font --font a.ttf [--fallback b.ttf] --out atlas.bsdf "
                   "[--em 32] [--spread 4] [--width 512]\n");
      return 2;
   }

   Font primary;
   Font fallback;
   if (!loadFont(fontPath, emPx, primary))
      return 1;
   const bool hasFallback = !fallbackPath.empty();
   if (hasFallback && !loadFont(fallbackPath, emPx, fallback))
      return 1;

   // stb encodes value = onedge + distance * pixelDistScale (positive inside).
   // With onedge 128 and scale 128/spread, d = 0..1 spans 2 * spread texels.
   const unsigned char onEdge = 128;
   const float pixelDistScale = 128.0f / static_cast<float>(spread);

   std::vector<Glyph> glyphs;
   for (uint32_t cp : charset())
   {
      const Font* font = nullptr;
      if (stbtt_FindGlyphIndex(&primary.info, static_cast<int>(cp)) != 0)
         font = &primary;
      else if (hasFallback && stbtt_FindGlyphIndex(&fallback.info, static_cast<int>(cp)) != 0)
         font = &fallback;
      if (!font)
      {
         std::fprintf(stderr, "bake_sdf_font: warning: U+%04X missing from all fonts\n", cp);
         continue;
      }

      Glyph g;
      g.codepoint = cp;
      g.font = font;
      int advance = 0;
      int lsb = 0;
      stbtt_GetCodepointHMetrics(&font->info, static_cast<int>(cp), &advance, &lsb);
      g.advance = static_cast<float>(advance) * font->scale / emPx;

      int xoff = 0;
      int yoff = 0;
      unsigned char* sdf = stbtt_GetCodepointSDF(&font->info, font->scale, static_cast<int>(cp), spread, onEdge,
                                                 pixelDistScale, &g.w, &g.h, &xoff, &yoff);
      if (sdf)
      {
         g.pixels.assign(sdf, sdf + static_cast<size_t>(g.w) * static_cast<size_t>(g.h));
         stbtt_FreeSDF(sdf, nullptr);
         g.left = static_cast<float>(xoff) / emPx;
         g.top = static_cast<float>(yoff) / emPx;
         g.right = static_cast<float>(xoff + g.w) / emPx;
         g.bottom = static_cast<float>(yoff + g.h) / emPx;
      }
      else
      {
         g.w = 0; // whitespace: advance only
         g.h = 0;
      }
      glyphs.push_back(std::move(g));
   }

   // Shelf-pack tallest-first for a tight atlas; the glyph table stays sorted by codepoint.
   std::vector<Glyph*> order;
   for (Glyph& g : glyphs)
      if (g.w > 0)
         order.push_back(&g);
   std::stable_sort(order.begin(), order.end(), [](const Glyph* a, const Glyph* b)
                    {
                       return a->h > b->h;
                    });

   int penX = kGutter;
   int penY = kGutter;
   int shelfHeight = 0;
   for (Glyph* g : order)
   {
      if (g->w + 2 * kGutter > atlasWidth)
      {
         std::fprintf(stderr, "bake_sdf_font: glyph U+%04X wider than atlas\n", g->codepoint);
         return 1;
      }
      if (penX + g->w + kGutter > atlasWidth)
      {
         penX = kGutter;
         penY += shelfHeight + kGutter;
         shelfHeight = 0;
      }
      g->x = penX;
      g->y = penY;
      penX += g->w + kGutter;
      shelfHeight = std::max(shelfHeight, g->h);
   }
   const int usedHeight = penY + shelfHeight + kGutter;
   // No mipmaps and clamp-to-edge sampling, so WebGL2/WebGPU take any height; round
   // to 8 rows rather than a power of two to keep the resource pack small.
   const int atlasHeight = (usedHeight + 7) & ~7;

   std::vector<unsigned char> atlas(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight), 0);
   for (const Glyph* g : order)
      for (int row = 0; row < g->h; ++row)
         std::memcpy(&atlas[static_cast<size_t>(g->y + row) * static_cast<size_t>(atlasWidth) + static_cast<size_t>(g->x)],
                     &g->pixels[static_cast<size_t>(row) * static_cast<size_t>(g->w)], static_cast<size_t>(g->w));

   // Kerning between ASCII pairs that come from the same font.
   std::vector<Kern> kerns;
   for (const Glyph& a : glyphs)
      for (const Glyph& b : glyphs)
      {
         if (a.codepoint > 126 || b.codepoint > 126 || a.font != b.font)
            continue;
         const int k = stbtt_GetCodepointKernAdvance(&a.font->info, static_cast<int>(a.codepoint),
                                                     static_cast<int>(b.codepoint));
         if (k != 0)
            kerns.push_back({ a.codepoint, b.codepoint, static_cast<float>(k) * a.font->scale / emPx });
      }

   int ascent = 0;
   int descent = 0;
   int lineGap = 0;
   stbtt_GetFontVMetrics(&primary.info, &ascent, &descent, &lineGap);

   std::vector<unsigned char> out;
   out.insert(out.end(), kMagic, kMagic + 4);
   put<uint32_t>(out, kVersion);
   put<uint16_t>(out, static_cast<uint16_t>(atlasWidth));
   put<uint16_t>(out, static_cast<uint16_t>(atlasHeight));
   put<float>(out, emPx);
   put<float>(out, 2.0f * static_cast<float>(spread));
   put<float>(out, static_cast<float>(ascent) * primary.scale / emPx);
   put<float>(out, static_cast<float>(-descent) * primary.scale / emPx);
   put<float>(out, static_cast<float>(lineGap) * primary.scale / emPx);
   put<uint32_t>(out, static_cast<uint32_t>(glyphs.size()));
   put<uint32_t>(out, static_cast<uint32_t>(kerns.size()));
   for (const Glyph& g : glyphs)
   {
      put<uint32_t>(out, g.codepoint);
      put<float>(out, g.advance);
      put<float>(out, g.left);
      put<float>(out, g.top);
      put<float>(out, g.right);
      put<float>(out, g.bottom);
      put<uint16_t>(out, static_cast<uint16_t>(g.x));
      put<uint16_t>(out, static_cast<uint16_t>(g.y));
      put<uint16_t>(out, static_cast<uint16_t>(g.w));
      put<uint16_t>(out, static_cast<uint16_t>(g.h));
   }
   for (const Kern& k : kerns)
   {
      put<uint32_t>(out, k.left);
      put<uint32_t>(out, k.right);
      put<float>(out, k.amount);
   }
   out.insert(out.end(), atlas.begin(), atlas.end());

   FILE* fp = std::fopen(outPath.c_str(), "wb");
   if (!fp || std::fwrite(out.data(), 1, out.size(), fp) != out.size())
   {
      std::fprintf(stderr, "bake_sdf_font: cannot write %s\n", outPath.c_str());
      if (fp)
         std::fclose(fp);
      return 1;
   }
   std::fclose(fp);

   std::printf("bake_sdf_font: %zu glyphs, %zu kern pairs, atlas %dx%d (%zu bytes), file %zu bytes\n", glyphs.size(),
               kerns.size(), atlasWidth, atlasHeight, atlas.size(), out.size());
   return 0;
}
