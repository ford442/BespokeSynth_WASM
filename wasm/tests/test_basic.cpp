#include "test_common.h"

// Basic math tests
void test_math()
{
   printf("\n=== Math Tests ===\n");

   TEST("Addition", 2 + 2 == 4);
   TEST("Multiplication", 3 * 4 == 12);
   TEST("Division", 10 / 2 == 5);

   float pi = 3.14159f;
   TEST("Float constant", pi > 3.14f && pi < 3.15f);
}

// Memory tests
void test_memory()
{
   printf("\n=== Memory Tests ===\n");

   int* arr = new int[100];
   TEST("Heap allocation", arr != nullptr);

   for (int i = 0; i < 100; i++)
   {
      arr[i] = i * 2;
   }

   TEST("Array write/read", arr[50] == 100);

   delete[] arr;
   TEST("Heap deallocation", true); // If we got here, it didn't crash
}

// String tests
void test_strings()
{
   printf("\n=== String Tests ===\n");

   std::string str = "Hello, WebAssembly!";
   TEST("String creation", !str.empty());
   TEST("String length", str.length() == 19);

   std::string concat = str + " Testing.";
   TEST("String concatenation", concat.length() > str.length());
}

// Vector tests
#include <vector>

void test_vectors()
{
   printf("\n=== Vector Tests ===\n");

   std::vector<float> vec;
   vec.push_back(1.0f);
   vec.push_back(2.0f);
   vec.push_back(3.0f);

   TEST("Vector push_back", vec.size() == 3);
   TEST("Vector access", vec[1] == 2.0f);

   vec.clear();
   TEST("Vector clear", vec.empty());
}

void test_pixel_font()
{
   printf("\n=== Pixel Font Tests ===\n");

   TEST("Glyph count", kPixelFontGlyphCount == 101);
   TEST("ASCII index for 'A'", pixelFontCharIndex('A') == 33);
   TEST("ASCII index for 'a'", pixelFontCharIndex('a') == 65);
   TEST("Lowercase index below clamp max", pixelFontCharIndex('z') == 90);
   TEST("Space index", pixelFontCharIndex(' ') == 0);

   const char* sharpUtf8 = "\xe2\x99\xaf";
   PixelFontGlyph sharpGlyph = pixelFontDecodeGlyph(sharpUtf8, 0, 3);
   TEST("UTF-8 sharp glyph index", sharpGlyph.index == kPixelFontGlyphSharp);
   TEST("UTF-8 sharp consumes 3 bytes", sharpGlyph.byteLength == 3);

   const float fontSize = 10.0f;
   const float spaceAdvance = pixelFontGlyphAdvance(0, fontSize);
   const float letterAdvance = pixelFontGlyphAdvance(pixelFontCharIndex('m'), fontSize);
   TEST("Space advance narrower than letters", spaceAdvance < letterAdvance);

   const float helloWidth = pixelFontTextWidth("hello", fontSize);
   const float approxFiveLetters = 5.0f * letterAdvance + 4.0f * fontSize * kPixelFontCharSpacingRatio;
   TEST("textWidth matches per-glyph advance", std::fabs(helloWidth - approxFiveLetters) < 0.01f);

   const float spacedWidth = pixelFontTextWidth("a b", fontSize);
   TEST("textWidth includes narrower space", spacedWidth > helloWidth && spacedWidth < pixelFontTextWidth("abc", fontSize));

   const uint32_t* cols = getPixelFontColumns();
   TEST("Font column table populated", cols != nullptr && cols[kPixelFontGlyphCount * 5 - 1] >= 0);
   TEST("Lowercase 'b' differs from uppercase 'B'",
        cols[pixelFontCharIndex('b') * 5] != cols[pixelFontCharIndex('B') * 5]);
}

// Builds a minimal .bsdf blob (see bake_sdf_font.cpp): 2x2 atlas, glyphs
// ' ', 'A', 'V' (advance 0.25 / 0.5 / 0.5 em) and one kern pair A,V = -0.1 em.
static std::vector<uint8_t> makeTestSdfAtlas()
{
   std::vector<uint8_t> out;
   auto put = [&out](const void* data, size_t n) {
      const uint8_t* b = static_cast<const uint8_t*>(data);
      out.insert(out.end(), b, b + n);
   };
   auto u16 = [&put](uint16_t v) { put(&v, 2); };
   auto u32 = [&put](uint32_t v) { put(&v, 4); };
   auto f32 = [&put](float v) { put(&v, 4); };

   put("BSDF", 4);
   u32(1);
   u16(2);
   u16(2);
   f32(32.0f); // em texels
   f32(8.0f);  // px range
   f32(0.9f);
   f32(0.2f);
   f32(0.0f);
   u32(3); // glyphs
   u32(1); // kerns
   const struct
   {
      uint32_t cp;
      float advance;
      uint16_t w;
   } glyphs[] = { { ' ', 0.25f, 0 }, { 'A', 0.5f, 1 }, { 'V', 0.5f, 1 } };
   for (const auto& g : glyphs)
   {
      u32(g.cp);
      f32(g.advance);
      f32(0.0f);
      f32(-0.7f);
      f32(0.5f);
      f32(0.0f);
      u16(g.cp == 'V' ? 1 : 0);
      u16(0);
      u16(g.w);
      u16(g.w);
   }
   u32('A');
   u32('V');
   f32(-0.1f);
   const uint8_t pixels[4] = { 10, 20, 30, 40 };
   put(pixels, sizeof(pixels));
   return out;
}

void test_sdf_font()
{
   printf("\n=== SDF Font Tests ===\n");

   uint32_t cp = 0;
   TEST("UTF-8 ASCII decode", decodeUtf8("A", 0, 1, cp) == 1 && cp == 'A');
   TEST("UTF-8 sharp decode", decodeUtf8("\xe2\x99\xaf", 0, 3, cp) == 3 && cp == 0x266F);
   TEST("UTF-8 truncated sequence -> U+FFFD", decodeUtf8("\xe2\x99", 0, 2, cp) == 1 && cp == 0xFFFD);
   TEST("UTF-8 stray continuation -> U+FFFD", decodeUtf8("\x80", 0, 1, cp) == 1 && cp == 0xFFFD);

   const std::vector<uint8_t> blob = makeTestSdfAtlas();
   SdfFont font;
   TEST("SDF atlas loads", font.loadFromMemory(blob.data(), blob.size()));
   TEST("SDF atlas size", font.atlasWidth() == 2 && font.atlasHeight() == 2 && font.atlasBytes() == 4);
   TEST("SDF atlas pixels", font.atlasPixels()[3] == 40);
   TEST("SDF px range", std::fabs(font.pxRange() - 8.0f) < 1e-6f);
   TEST("SDF glyph lookup", font.findGlyph('A') && font.findGlyph('V') && !font.findGlyph('B'));
   TEST("SDF glyph UV", font.findGlyph('V') && std::fabs(font.findGlyph('V')->u0 - 0.5f) < 1e-6f);
   TEST("SDF kerning", std::fabs(font.kerning('A', 'V') + 0.1f) < 1e-6f && font.kerning('V', 'A') == 0.0f);

   const float fontSize = 10.0f;
   const float em = fontSize * kSdfFontEmScale;
   TEST("SDF textWidth applies kerning", std::fabs(font.textWidth("AV", fontSize) - (0.9f * em)) < 1e-4f);
   TEST("SDF unknown glyph advances like space", std::fabs(font.textWidth("?", fontSize) - 0.25f * em) < 1e-4f);

   int quads = 0;
   float lastX0 = -1.0f;
   font.layout("A V", 5.0f, 20.0f, fontSize, [&](const SdfGlyphQuad& q) {
      ++quads;
      lastX0 = q.x0;
      TEST("SDF quad sits on the baseline", std::fabs(q.y1 - 20.0f) < 1e-4f);
   });
   TEST("SDF layout skips whitespace quads", quads == 2);
   TEST("SDF layout pen matches textWidth", std::fabs(lastX0 - (5.0f + font.textWidth("A ", fontSize))) < 1e-4f);

   std::vector<uint8_t> truncated(blob.begin(), blob.end() - 1);
   SdfFont bad;
   TEST("SDF rejects truncated blob", !bad.loadFromMemory(truncated.data(), truncated.size()) && !bad.isLoaded());
   std::vector<uint8_t> wrongMagic = blob;
   wrongMagic[0] = 'X';
   TEST("SDF rejects wrong magic", !bad.loadFromMemory(wrongMagic.data(), wrongMagic.size()));

   setUiFontMode(UiFontMode::Pixel);
   TEST("Pixel font mode disables SDF text", !useSdfTextForFace(nullptr));
   setUiFontMode(UiFontMode::Sdf);
   TEST("Pixel face disables SDF text", !useSdfTextForFace(kPixelFontFaceName));
}

// Audio buffer simulation
void test_audio_buffer()
{
   printf("\n=== Audio Buffer Tests ===\n");

   const int bufferSize = 512;
   const int numChannels = 2;

   float** buffer = new float*[numChannels];
   for (int ch = 0; ch < numChannels; ch++)
   {
      buffer[ch] = new float[bufferSize];
      for (int i = 0; i < bufferSize; i++)
      {
         buffer[ch][i] = 0.0f;
      }
   }

   TEST("Buffer allocation", buffer != nullptr);
   TEST("Buffer initialization", buffer[0][0] == 0.0f);

   // Generate sine wave
   float phase = 0.0f;
   float phaseInc = 2.0f * 3.14159f * 440.0f / 44100.0f;

   for (int i = 0; i < bufferSize; i++)
   {
      float sample = sinf(phase);
      buffer[0][i] = sample;
      buffer[1][i] = sample;
      phase += phaseInc;
   }

   TEST("Sine generation", buffer[0][0] != 0.0f || buffer[0][100] != 0.0f);

   for (int ch = 0; ch < numChannels; ch++)
   {
      delete[] buffer[ch];
   }
   delete[] buffer;

   TEST("Buffer cleanup", true);
}
