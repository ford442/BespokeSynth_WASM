/**
 * BespokeSynth WASM - Test Suite
 * Basic tests for WASM build verification
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include <emscripten.h>
#include <cstdio>
#include <cmath>
#include <string>
#include "PixelFont.h"

using namespace bespoke::wasm;

// Test results
static int gTestsPassed = 0;
static int gTestsFailed = 0;

#define TEST(name, condition) \
    do { \
        if (condition) { \
            printf("[PASS] %s\n", name); \
            gTestsPassed++; \
        } else { \
            printf("[FAIL] %s\n", name); \
            gTestsFailed++; \
        } \
    } while (0)

// Basic math tests
void test_math() {
    printf("\n=== Math Tests ===\n");
    
    TEST("Addition", 2 + 2 == 4);
    TEST("Multiplication", 3 * 4 == 12);
    TEST("Division", 10 / 2 == 5);
    
    float pi = 3.14159f;
    TEST("Float constant", pi > 3.14f && pi < 3.15f);
}

// Memory tests
void test_memory() {
    printf("\n=== Memory Tests ===\n");
    
    int* arr = new int[100];
    TEST("Heap allocation", arr != nullptr);
    
    for (int i = 0; i < 100; i++) {
        arr[i] = i * 2;
    }
    
    TEST("Array write/read", arr[50] == 100);
    
    delete[] arr;
    TEST("Heap deallocation", true);  // If we got here, it didn't crash
}

// String tests
void test_strings() {
    printf("\n=== String Tests ===\n");
    
    std::string str = "Hello, WebAssembly!";
    TEST("String creation", !str.empty());
    TEST("String length", str.length() == 19);
    
    std::string concat = str + " Testing.";
    TEST("String concatenation", concat.length() > str.length());
}

// Vector tests
#include <vector>

void test_vectors() {
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

void test_pixel_font() {
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

// Audio buffer simulation
void test_audio_buffer() {
    printf("\n=== Audio Buffer Tests ===\n");
    
    const int bufferSize = 512;
    const int numChannels = 2;
    
    float** buffer = new float*[numChannels];
    for (int ch = 0; ch < numChannels; ch++) {
        buffer[ch] = new float[bufferSize];
        for (int i = 0; i < bufferSize; i++) {
            buffer[ch][i] = 0.0f;
        }
    }
    
    TEST("Buffer allocation", buffer != nullptr);
    TEST("Buffer initialization", buffer[0][0] == 0.0f);
    
    // Generate sine wave
    float phase = 0.0f;
    float phaseInc = 2.0f * 3.14159f * 440.0f / 44100.0f;
    
    for (int i = 0; i < bufferSize; i++) {
        float sample = sinf(phase);
        buffer[0][i] = sample;
        buffer[1][i] = sample;
        phase += phaseInc;
    }
    
    TEST("Sine generation", buffer[0][0] != 0.0f || buffer[0][100] != 0.0f);
    
    for (int ch = 0; ch < numChannels; ch++) {
        delete[] buffer[ch];
    }
    delete[] buffer;
    
    TEST("Buffer cleanup", true);
}

int main() {
    printf("BespokeSynth WASM Test Suite\n");
    printf("============================\n");
    
    test_math();
    test_memory();
    test_strings();
    test_vectors();
    test_pixel_font();
    test_audio_buffer();
    
    printf("\n============================\n");
    printf("Results: %d passed, %d failed\n", gTestsPassed, gTestsFailed);
    printf("============================\n");
    
    return gTestsFailed > 0 ? 1 : 0;
}
