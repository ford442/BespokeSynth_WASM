/**
 * BespokeSynth WASM - Shared canvas rendering helpers
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/PixelFont.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         std::string lowercase(const std::string& value)
         {
            std::string result = value;
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c)
                           {
                              return static_cast<char>(std::tolower(c));
                           });
            return result;
         }

         bool fuzzyMatches(const ModuleTypeInfo& type, const std::string& query)
         {
            if (query.empty())
               return true;

            const std::string candidate = lowercase(type.displayName + " " + type.type);
            size_t queryIndex = 0;
            for (char c : candidate)
            {
               if (queryIndex < query.size() && c == query[queryIndex])
                  ++queryIndex;
            }
            return queryIndex == query.size();
         }
      }

      float textBaselineFromTop(float topY, float fontSize)
      {
         return topY + fontSize * kPixelFontBaselineRatio;
      }

      float textBaselineFromTop(Renderer2D& renderer, float topY)
      {
         return textBaselineFromTop(topY, renderer.textHeight());
      }

      float textBaselineCentered(float centerY, float fontSize)
      {
         return centerY + fontSize * (kPixelFontBaselineRatio - 0.5f);
      }

      float textLineSpacing(float fontSize, float scale)
      {
         return fontSize + 4.0f * scale;
      }

      float textBaselineForLine(float contentTop, int lineIndex, float fontSize, float scale)
      {
         const float lineStep = textLineSpacing(fontSize, scale);
         return textBaselineFromTop(contentTop + static_cast<float>(lineIndex) * lineStep, fontSize);
      }

      void drawText(Renderer2D& renderer, float x, float y, const char* text,
                    const Color& color, float fontSize)
      {
         renderer.fillColor(color);
         renderer.fontSize(fontSize);
         renderer.text(x, y, text);
      }

      void truncateWithEllipsis(Renderer2D& renderer, const char* text, float maxWidth,
                                char* out, size_t outLen)
      {
         if (!text || !out || outLen == 0)
            return;

         if (renderer.textWidth(text) <= maxWidth)
         {
            snprintf(out, outLen, "%s", text);
            return;
         }

         const char* ellipsis = "...";
         const size_t srcLen = strlen(text);
         size_t keep = srcLen;

         while (keep > 0)
         {
            char trial[128];
            const size_t copyLen = std::min(keep, sizeof(trial) - 4);
            memcpy(trial, text, copyLen);
            trial[copyLen] = '\0';
            snprintf(out, outLen, "%s%s", trial, ellipsis);
            if (renderer.textWidth(out) <= maxWidth)
               return;
            --keep;
         }

         snprintf(out, outLen, "%s", ellipsis);
      }

      void formatFrequency(float hz, char* buf, size_t buflen)
      {
         if (hz >= 1000.0f)
            snprintf(buf, buflen, "%.2f kHz", hz / 1000.0f);
         else
            snprintf(buf, buflen, "%.1f Hz", hz);
      }

      void formatGainDb(float gain, char* buf, size_t buflen)
      {
         if (gain <= 0.0001f)
            snprintf(buf, buflen, "-inf dB");
         else
            snprintf(buf, buflen, "%.1f dB", 20.0f * log10f(gain));
      }

      float waveformSample(int waveform, float t)
      {
         const float phase = fmodf(t, 1.0f) * 6.2831853f;
         switch (waveform % 4)
         {
            case 1:
               return 2.0f * fmodf(t, 1.0f) - 1.0f;
            case 2:
               return (fmodf(t, 1.0f) < 0.5f) ? 1.0f : -1.0f;
            case 3:
               return fabsf(2.0f * fmodf(t + 0.25f, 1.0f) - 1.0f) * 2.0f - 1.0f;
            default:
               return sinf(phase);
         }
      }

      float portSpacingFor(float labelFont, float scale)
      {
         return std::max(15.0f * scale, textLineSpacing(labelFont, scale));
      }

      std::vector<ModuleTypeInfo> spawnMenuTypes(int category, const std::string& query)
      {
         std::vector<ModuleTypeInfo> result;
         for (const auto& type : WasmModuleAdapterRegistry::instance().registeredTypes())
         {
            if (type.type == "transport" || type.type == "scale")
               continue;
            if (category >= 0 && static_cast<int>(type.category) != category)
               continue;
            if (fuzzyMatches(type, query))
               result.push_back(type);
         }
         std::sort(result.begin(), result.end(), [](const ModuleTypeInfo& a, const ModuleTypeInfo& b)
                   {
                      return a.displayName < b.displayName;
                   });
         return result;
      }

   } // namespace wasm
} // namespace bespoke
