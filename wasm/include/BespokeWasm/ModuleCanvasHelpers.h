/**
 * BespokeSynth WASM - Shared canvas rendering helpers
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/ModuleTypes.h"
#include "BespokeWasm/Renderer2D.h"
#include <cstddef>
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      float textBaselineFromTop(float topY, float fontSize);
      float textBaselineFromTop(Renderer2D& renderer, float topY);
      float textBaselineCentered(float centerY, float fontSize);
      float textLineSpacing(float fontSize, float scale);
      float textBaselineForLine(float contentTop, int lineIndex, float fontSize, float scale);

      void drawText(Renderer2D& renderer, float x, float y, const char* text,
                    const Color& color, float fontSize);
      void truncateWithEllipsis(Renderer2D& renderer, const char* text, float maxWidth,
                                char* out, size_t outLen);
      void formatFrequency(float hz, char* buf, size_t buflen);
      void formatGainDb(float gain, char* buf, size_t buflen);
      float waveformSample(int waveform, float t);
      float portSpacingFor(float labelFont, float scale);

      std::vector<ModuleTypeInfo> spawnMenuTypes(int category, const std::string& query);

   } // namespace wasm
} // namespace bespoke
