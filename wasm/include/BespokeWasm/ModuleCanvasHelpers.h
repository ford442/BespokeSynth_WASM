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

      /** Spawn menu category chips, drawn and hit-tested from this one table. */
      struct SpawnMenuChip
      {
         const char* label;
         int category; // ModuleCategory, or -1 for All
      };
      constexpr int kSpawnMenuChipCount = 7;
      constexpr float kSpawnMenuChipPitch = 38.0f;
      constexpr float kSpawnMenuChipWidth = 36.0f;
      const SpawnMenuChip& spawnMenuChip(int index);

      std::vector<ModuleTypeInfo> spawnMenuTypes(int category, const std::string& query);
      /** Feature-flagged types (e.g. "script" without ?script=1) stay out of the spawn menu. */
      void setSpawnMenuTypeHidden(const std::string& type, bool hidden);

   } // namespace wasm
} // namespace bespoke
