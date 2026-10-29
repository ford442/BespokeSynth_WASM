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

      std::vector<std::string> wrapText(Renderer2D& renderer, const std::string& text, float maxWidth,
                                        int maxLines)
      {
         std::vector<std::string> lines;
         bool cut = false;
         size_t paragraphStart = 0;
         while (paragraphStart <= text.size() && !cut)
         {
            size_t paragraphEnd = text.find('\n', paragraphStart);
            if (paragraphEnd == std::string::npos)
               paragraphEnd = text.size();
            std::string line;
            size_t wordStart = paragraphStart;
            while (wordStart < paragraphEnd)
            {
               size_t wordEnd = text.find(' ', wordStart);
               if (wordEnd == std::string::npos || wordEnd > paragraphEnd)
                  wordEnd = paragraphEnd;
               const std::string word = text.substr(wordStart, wordEnd - wordStart);
               const std::string candidate = line.empty() ? word : line + " " + word;
               if (!line.empty() && renderer.textWidth(candidate.c_str()) > maxWidth)
               {
                  if (maxLines > 0 && static_cast<int>(lines.size()) + 1 >= maxLines)
                  {
                     cut = true;
                     break;
                  }
                  lines.push_back(line);
                  line = word;
               }
               else
               {
                  line = candidate;
               }
               wordStart = wordEnd + 1;
            }
            if (cut)
            {
               // Make room for the ellipsis on the last kept line.
               while (!line.empty() && renderer.textWidth((line + "...").c_str()) > maxWidth)
                  line.pop_back();
               lines.push_back(line + "...");
               break;
            }
            lines.push_back(line);
            paragraphStart = paragraphEnd + 1;
            if (paragraphEnd < text.size() && maxLines > 0 && static_cast<int>(lines.size()) >= maxLines)
            {
               lines.back() += "...";
               break;
            }
         }
         return lines;
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

      namespace
      {
         std::vector<std::string>& hiddenSpawnTypes()
         {
            static std::vector<std::string> hidden;
            return hidden;
         }
      }

      void setSpawnMenuTypeHidden(const std::string& type, bool hidden)
      {
         auto& list = hiddenSpawnTypes();
         auto it = std::find(list.begin(), list.end(), type);
         if (hidden && it == list.end())
            list.push_back(type);
         else if (!hidden && it != list.end())
            list.erase(it);
      }

      const SpawnMenuChip& spawnMenuChip(int index)
      {
         static const SpawnMenuChip chips[kSpawnMenuChipCount] = {
            { "All", -1 },
            { "Synth", static_cast<int>(ModuleCategory::Synth) },
            { "Note", static_cast<int>(ModuleCategory::NoteEffect) },
            { "FX", static_cast<int>(ModuleCategory::AudioEffect) },
            { "Mod", static_cast<int>(ModuleCategory::Modulator) },
            { "Pulse", static_cast<int>(ModuleCategory::Pulse) },
            { "Other", static_cast<int>(ModuleCategory::Other) },
         };
         return chips[std::max(0, std::min(kSpawnMenuChipCount - 1, index))];
      }

      std::vector<ModuleTypeInfo> spawnMenuTypes(int category, const std::string& query)
      {
         std::vector<ModuleTypeInfo> result;
         const auto& hidden = hiddenSpawnTypes();
         for (const auto& type : WasmModuleAdapterRegistry::instance().registeredTypes())
         {
            if (type.type == "transport" || type.type == "scale")
               continue;
            if (std::find(hidden.begin(), hidden.end(), type.type) != hidden.end())
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
