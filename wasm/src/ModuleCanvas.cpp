/**
 * BespokeSynth WASM - Modular Canvas Implementation
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/PixelFont.h"
#include "BespokeWasm/AudioAnalysis.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <array>

namespace bespoke
{
   namespace wasm
   {

      namespace
      {

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

         std::vector<ModuleTypeInfo> spawnMenuTypes(int category, const std::string& query)
         {
            std::vector<ModuleTypeInfo> result;
            for (const auto& type : ModuleFactory::instance().getRegisteredTypes())
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

         std::vector<std::string> serializableControlsForType(const std::string& type)
         {
            if (type == "oscillator")
               return { "frequency", "volume", "waveform" };
            if (type == "filter")
               return { "cutoff", "resonance", "type" };
            if (type == "gain")
               return { "gain" };
            if (type == "output")
               return { "level" };
            if (type == "lfo")
               return { "rate", "depth", "shape" };
            if (type == "transport")
               return { "bpm", "swing" };
            if (type == "scale")
               return { "root", "type" };
            return {};
         }

      } // namespace

      // ============================================================
      // Module base class
      // ============================================================

      Module::Module(int id, const std::string& type, const std::string& name, ModuleCategory category)
      : mId(id)
      , mType(type)
      , mName(name)
      , mCategory(category)
      , mX(0)
      , mY(0)
      , mWidth(150)
      , mHeight(100)
      , mEnabled(true)
      , mMinimized(false)
      {
      }

      void Module::addInput(const std::string& name, PortType type)
      {
         Port p(name, type, false);
         mInputs.push_back(p);
      }

      void Module::addOutput(const std::string& name, PortType type)
      {
         Port p(name, type, true);
         mOutputs.push_back(p);
      }

      bool Module::hitTest(float worldX, float worldY) const
      {
         return worldX >= mX && worldX <= mX + mWidth &&
                worldY >= mY && worldY <= mY + mHeight;
      }

      bool Module::hitTitleBar(float worldX, float worldY) const
      {
         return worldX >= mX && worldX <= mX + mWidth &&
                worldY >= mY && worldY <= mY + kTitleBarHeight;
      }

      void Module::renderTitleBar(Renderer2D& renderer, float screenX, float screenY, float scale)
      {
         float w = mWidth * scale;
         float h = kTitleBarHeight * scale;

         // Title bar background - color-coded by category
         Color catColor;
         switch (mCategory)
         {
            case ModuleCategory::Instrument:
               catColor = Color(0.2f, 0.5f, 0.8f, 1.0f);
               break;
            case ModuleCategory::NoteEffect:
               catColor = Color(0.6f, 0.3f, 0.7f, 1.0f);
               break;
            case ModuleCategory::Synth:
               catColor = Color(0.3f, 0.7f, 0.5f, 1.0f);
               break;
            case ModuleCategory::AudioEffect:
               catColor = Color(0.8f, 0.5f, 0.2f, 1.0f);
               break;
            case ModuleCategory::Modulator:
               catColor = Color(0.7f, 0.7f, 0.2f, 1.0f);
               break;
            case ModuleCategory::Pulse:
               catColor = Color(0.8f, 0.3f, 0.3f, 1.0f);
               break;
            default:
               catColor = Color(0.4f, 0.4f, 0.45f, 1.0f);
               break;
         }

         renderer.fillColor(catColor);
         renderer.roundedRect(screenX, screenY, w, h, 4.0f * scale);
         renderer.fill();

         // Module name (theme) — ellipsize when wider than title bar
         const float titleFont = 11.0f * scale;
         const float titlePad = 5.0f * scale;
         const float titleRightPad = (mEnabled ? 16.0f : 8.0f) * scale;
         char titleBuf[64];
         renderer.fontSize(titleFont);
         truncateWithEllipsis(renderer, mName.c_str(), w - titlePad - titleRightPad,
                              titleBuf, sizeof(titleBuf));
         drawText(renderer, screenX + titlePad,
                  textBaselineFromTop(renderer, screenY + 2 * scale),
                  titleBuf, UITheme::kTextPrimary, titleFont);

         // Enabled indicator
         if (!mEnabled)
         {
            renderer.fillColor(Color(0.8f, 0.2f, 0.2f, 0.7f));
            renderer.circle(screenX + w - 8 * scale, screenY + h * 0.5f, 3 * scale);
            renderer.fill();
         }
      }

      void Module::renderPorts(Renderer2D& renderer, float screenX, float screenY, float scale)
      {
         const float labelFont = 9.0f * scale;
         const float portStep = portSpacingFor(labelFont, scale);
         const float labelOffsetX = 8.0f * scale;

         renderer.fontSize(labelFont);

         // Input ports on the left
         for (size_t i = 0; i < mInputs.size(); i++)
         {
            float px = screenX;
            float py = screenY + (kTitleBarHeight + 10) * scale + static_cast<float>(i) * portStep;

            Color portColor;
            switch (mInputs[i].type)
            {
               case PortType::Audio:
                  portColor = Color(0.3f, 0.7f, 0.9f, 1.0f);
                  break;
               case PortType::Note:
                  portColor = Color(0.9f, 0.7f, 0.3f, 1.0f);
                  break;
               case PortType::Pulse:
                  portColor = Color(0.9f, 0.3f, 0.3f, 1.0f);
                  break;
               case PortType::Modulation:
                  portColor = Color(0.5f, 0.9f, 0.4f, 1.0f);
                  break;
            }

            renderer.fillColor(portColor);
            renderer.circle(px, py, kPortRadius * scale);
            renderer.fill();

            const float inputLabelW = renderer.textWidth(mInputs[i].name.c_str());
            float inputLabelX = px + labelOffsetX;
            const float inputMaxX = screenX + mWidth * scale - 4.0f * scale - inputLabelW;
            if (inputLabelX > inputMaxX)
               inputLabelX = std::max(screenX + labelOffsetX, inputMaxX);

            drawText(renderer, inputLabelX,
                     textBaselineCentered(py, labelFont),
                     mInputs[i].name.c_str(), UITheme::kTextSecondary, labelFont);
         }

         // Output ports on the right
         float moduleRight = screenX + mWidth * scale;
         for (size_t i = 0; i < mOutputs.size(); i++)
         {
            float px = moduleRight;
            float py = screenY + (kTitleBarHeight + 10) * scale + static_cast<float>(i) * portStep;

            Color portColor;
            switch (mOutputs[i].type)
            {
               case PortType::Audio:
                  portColor = Color(0.3f, 0.7f, 0.9f, 1.0f);
                  break;
               case PortType::Note:
                  portColor = Color(0.9f, 0.7f, 0.3f, 1.0f);
                  break;
               case PortType::Pulse:
                  portColor = Color(0.9f, 0.3f, 0.3f, 1.0f);
                  break;
               case PortType::Modulation:
                  portColor = Color(0.5f, 0.9f, 0.4f, 1.0f);
                  break;
            }

            renderer.fillColor(portColor);
            renderer.circle(px, py, kPortRadius * scale);
            renderer.fill();

            const float labelW = renderer.textWidth(mOutputs[i].name.c_str());
            float outputLabelX = px - labelOffsetX - labelW;
            const float outputMinX = screenX + 4.0f * scale;
            if (outputLabelX < outputMinX)
               outputLabelX = outputMinX;

            drawText(renderer, outputLabelX,
                     textBaselineCentered(py, labelFont),
                     mOutputs[i].name.c_str(), UITheme::kTextSecondary, labelFont);
         }
      }

      void Module::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         float w = mWidth * scale;
         float h = mHeight * scale;

         // Module body
         renderer.fillColor(Color(0.16f, 0.16f, 0.18f, 0.95f));
         renderer.roundedRect(screenX, screenY, w, h, 6.0f * scale);
         renderer.fill();

         // Module border
         renderer.strokeColor(Color(0.35f, 0.35f, 0.4f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(screenX, screenY, w, h, 6.0f * scale);
         renderer.stroke();

         // Title bar and ports
         renderTitleBar(renderer, screenX, screenY, scale);
         renderPorts(renderer, screenX, screenY, scale);
      }

      // ============================================================
      // OscillatorModule
      // ============================================================

      OscillatorModule::OscillatorModule(int id)
      : Module(id, "oscillator", "Oscillator", ModuleCategory::Synth)
      {
         setSize(160, 120);
         addInput("Pitch", PortType::Note);
         addInput("Mod", PortType::Modulation);
         addOutput("Out", PortType::Audio);
      }

      void OscillatorModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float labelFont = UITheme::kLabelFontSize * scale;
         const float lineFont = std::max(valueFont, labelFont);
         const float lineH = textLineSpacing(lineFont, scale);

         const float contentTop = screenY + (kTitleBarHeight + 22) * scale;
         const float textX = screenX + 10 * scale;

         char freqText[32];
         formatFrequency(mFrequency, freqText, sizeof(freqText));
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, lineFont, scale),
                  freqText, UITheme::kTextValue, valueFont);

         const char* waveNames[] = { "Sine", "Saw", "Square", "Triangle" };
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, lineFont, scale),
                  waveNames[mWaveform % 4], UITheme::kTextPrimary, labelFont);

         char volText[32];
         snprintf(volText, sizeof(volText), "Level %.0f%%", mVolume * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 2, lineFont, scale),
                  volText, UITheme::kTextSecondary, valueFont);

         // Mini waveform preview (matches selected shape)
         float wfX = screenX + 10 * scale;
         float wfY = contentTop + lineH * 3.0f;
         float wfW = 70 * scale;
         float wfH = 22 * scale;

         renderer.strokeColor(Color(0.3f, 0.8f, 0.5f, 0.85f));
         renderer.strokeWidth(1.5f * scale);
         for (int i = 0; i < 24; i++)
         {
            float t1 = (float)i / 24.0f;
            float t2 = (float)(i + 1) / 24.0f;
            float y1 = waveformSample(mWaveform, t1) * 0.45f;
            float y2 = waveformSample(mWaveform, t2) * 0.45f;
            renderer.line(wfX + t1 * wfW, wfY + wfH * 0.5f - y1 * wfH,
                          wfX + t2 * wfW, wfY + wfH * 0.5f - y2 * wfH);
         }
      }

      void OscillatorModule::setControlValue(const std::string& name, float value)
      {
         if (name == "frequency")
            mFrequency = value;
         else if (name == "volume")
            mVolume = value;
         else if (name == "waveform")
            mWaveform = static_cast<int>(value);
      }

      float OscillatorModule::getControlValue(const std::string& name) const
      {
         if (name == "frequency")
            return mFrequency;
         if (name == "volume")
            return mVolume;
         if (name == "waveform")
            return static_cast<float>(mWaveform);
         return 0.0f;
      }

      bool OscillatorModule::handleMouseDown(float worldX, float worldY)
      {
         const float localX = worldX - mX;
         const float localY = worldY - mY;

         // Click waveform name row to cycle shape
         if (localX >= 10.0f && localX <= mWidth - 10.0f &&
             localY >= kTitleBarHeight + 34.0f && localY <= kTitleBarHeight + 50.0f)
         {
            mWaveform = (mWaveform + 1) % 4;
            return true;
         }

         // Drag frequency in the upper content area
         if (localX >= 10.0f && localX <= mWidth - 10.0f &&
             localY >= kTitleBarHeight + 18.0f && localY <= kTitleBarHeight + 70.0f)
         {
            mDraggingFreq = true;
            return true;
         }
         return false;
      }

      bool OscillatorModule::handleMouseDrag(float worldX, float worldY, float dx, float dy)
      {
         (void)worldX;
         (void)worldY;
         if (!mDraggingFreq)
            return false;

         const float mult = powf(2.0f, -dy * 0.04f);
         mFrequency = std::max(20.0f, std::min(20000.0f, mFrequency * mult));
         return true;
      }

      void OscillatorModule::handleMouseUp()
      {
         mDraggingFreq = false;
      }

      // ============================================================
      // GainModule
      // ============================================================

      GainModule::GainModule(int id)
      : Module(id, "gain", "Gain", ModuleCategory::AudioEffect)
      {
         setSize(120, 80);
         addInput("In", PortType::Audio);
         addInput("Mod", PortType::Modulation);
         addOutput("Out", PortType::Audio);
      }

      void GainModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         // Draw gain slider
         float sliderX = screenX + 10 * scale;
         float sliderY = screenY + (kTitleBarHeight + 20) * scale;
         float sliderW = 100 * scale;
         float sliderH = 16 * scale;

         renderer.drawSlider(sliderX, sliderY, sliderW, sliderH,
                             mGain,
                             Color(0.2f, 0.2f, 0.22f, 1.0f),
                             Color(0.8f, 0.5f, 0.2f, 1.0f));

         char gainText[16];
         formatGainDb(mGain, gainText, sizeof(gainText));
         drawText(renderer, sliderX, textBaselineFromTop(renderer, sliderY + sliderH + 4 * scale),
                  gainText, UITheme::kTextValue, UITheme::kValueFontSize * scale);
      }

      void GainModule::setControlValue(const std::string& name, float value)
      {
         if (name == "gain")
            mGain = value;
      }

      float GainModule::getControlValue(const std::string& name) const
      {
         if (name == "gain")
            return mGain;
         return 0.0f;
      }

      bool GainModule::handleMouseDown(float worldX, float worldY)
      {
         const float sliderX = mX + 10.0f;
         const float sliderY = mY + kTitleBarHeight + 20.0f;
         const float sliderW = 100.0f;
         const float sliderH = 16.0f;

         if (worldX >= sliderX && worldX <= sliderX + sliderW &&
             worldY >= sliderY && worldY <= sliderY + sliderH)
         {
            mDraggingSlider = true;
            mGain = std::max(0.0f, std::min(1.0f, (worldX - sliderX) / sliderW));
            return true;
         }
         return false;
      }

      bool GainModule::handleMouseDrag(float worldX, float worldY, float dx, float dy)
      {
         (void)worldY;
         (void)dx;
         (void)dy;
         if (!mDraggingSlider)
            return false;

         const float sliderX = mX + 10.0f;
         const float sliderW = 100.0f;
         mGain = std::max(0.0f, std::min(1.0f, (worldX - sliderX) / sliderW));
         return true;
      }

      void GainModule::handleMouseUp()
      {
         mDraggingSlider = false;
      }

      // ============================================================
      // OutputModule
      // ============================================================

      OutputModule::OutputModule(int id)
      : Module(id, "output", "Output", ModuleCategory::Other)
      {
         setSize(120, 90);
         addInput("In", PortType::Audio);
      }

      void OutputModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         // Draw VU meter
         float meterX = screenX + 20 * scale;
         float meterY = screenY + (kTitleBarHeight + 15) * scale;
         renderer.drawVUMeter(meterX, meterY, 15 * scale, 45 * scale,
                              mLevel,
                              Color(0.2f, 0.8f, 0.3f, 1.0f),
                              Color(1.0f, 0.2f, 0.1f, 1.0f));

         renderer.drawVUMeter(meterX + 25 * scale, meterY, 15 * scale, 45 * scale,
                              mLevel * 0.9f,
                              Color(0.2f, 0.8f, 0.3f, 1.0f),
                              Color(1.0f, 0.2f, 0.1f, 1.0f));

         const float meterLabelFont = 9.0f * scale;
         const float meterLabelY = textBaselineFromTop(renderer, meterY + 48 * scale);
         drawText(renderer, meterX, meterLabelY, "L", UITheme::kTextSecondary, meterLabelFont);
         drawText(renderer, meterX + 25 * scale, meterLabelY, "R",
                  UITheme::kTextSecondary, meterLabelFont);
      }

      void OutputModule::setControlValue(const std::string& name, float value)
      {
         if (name == "level")
            mLevel = std::max(0.0f, std::min(1.0f, value));
      }

      float OutputModule::getControlValue(const std::string& name) const
      {
         if (name == "level")
            return mLevel;
         return 0.0f;
      }

      // ============================================================
      // FilterModule
      // ============================================================

      FilterModule::FilterModule(int id)
      : Module(id, "filter", "Filter", ModuleCategory::AudioEffect)
      {
         setSize(150, 110);
         addInput("In", PortType::Audio);
         addInput("CV", PortType::Modulation);
         addOutput("Out", PortType::Audio);
      }

      void FilterModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         const float contentTop = screenY + (kTitleBarHeight + 18) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float labelFont = UITheme::kLabelFontSize * scale;
         const float lineFont = std::max(valueFont, labelFont);
         const float lineH = textLineSpacing(lineFont, scale);
         const float textX = screenX + 10 * scale;

         const char* typeNames[] = { "Low-pass", "High-pass", "Band-pass" };
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, lineFont, scale),
                  typeNames[mType % 3], UITheme::kTextPrimary, labelFont);

         char cutText[48];
         snprintf(cutText, sizeof(cutText), "Cutoff ");
         formatFrequency(mCutoff, cutText + 7, sizeof(cutText) - 7);
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, lineFont, scale),
                  cutText, UITheme::kTextValue, valueFont);

         char resText[32];
         snprintf(resText, sizeof(resText), "Resonance %.2f", mResonance);
         drawText(renderer, textX, textBaselineForLine(contentTop, 2, lineFont, scale),
                  resText, UITheme::kTextSecondary, valueFont);

         // Simple filter response curve
         float curveX = screenX + 10 * scale;
         float curveY = contentTop + lineH * 3.0f;
         float curveW = 80 * scale;
         float curveH = 20 * scale;

         renderer.strokeColor(Color(0.8f, 0.5f, 0.2f, 0.8f));
         renderer.strokeWidth(1.5f * scale);
         for (int i = 0; i < 15; i++)
         {
            float t1 = (float)i / 15.0f;
            float t2 = (float)(i + 1) / 15.0f;
            float cutNorm = mCutoff / 20000.0f;
            float y1 = (t1 < cutNorm) ? 1.0f : expf(-(t1 - cutNorm) * 10.0f);
            float y2 = (t2 < cutNorm) ? 1.0f : expf(-(t2 - cutNorm) * 10.0f);
            renderer.line(curveX + t1 * curveW, curveY + curveH - y1 * curveH,
                          curveX + t2 * curveW, curveY + curveH - y2 * curveH);
         }
      }

      void FilterModule::setControlValue(const std::string& name, float value)
      {
         if (name == "cutoff")
            mCutoff = value;
         else if (name == "resonance")
            mResonance = value;
         else if (name == "type")
            mType = static_cast<int>(value);
      }

      float FilterModule::getControlValue(const std::string& name) const
      {
         if (name == "cutoff")
            return mCutoff;
         if (name == "resonance")
            return mResonance;
         if (name == "type")
            return static_cast<float>(mType);
         return 0.0f;
      }

      // ============================================================
      // LFOModule
      // ============================================================

      LFOModule::LFOModule(int id)
      : Module(id, "lfo", "LFO", ModuleCategory::Modulator)
      {
         setSize(130, 100);
         addOutput("Mod", PortType::Modulation);
      }

      void LFOModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         const float contentTop = screenY + (kTitleBarHeight + 14) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float labelFont = UITheme::kLabelFontSize * scale;
         const float lineFont = std::max(valueFont, labelFont);
         const float lineH = textLineSpacing(lineFont, scale);
         const float textX = screenX + 10 * scale;

         const char* shapeNames[] = { "Sine", "Triangle", "Saw", "Square" };

         char rateText[32];
         snprintf(rateText, sizeof(rateText), "Rate %.2f Hz", mRate);
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, lineFont, scale),
                  rateText, UITheme::kTextValue, valueFont);

         char depthText[32];
         snprintf(depthText, sizeof(depthText), "Depth %.0f%%", mDepth * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, lineFont, scale),
                  depthText, UITheme::kTextSecondary, valueFont);

         drawText(renderer, textX, textBaselineForLine(contentTop, 2, lineFont, scale),
                  shapeNames[mShape % 4], UITheme::kTextPrimary, labelFont);

         // Draw LFO waveform
         float wfX = screenX + 10 * scale;
         float wfY = contentTop + lineH * 3.0f;
         float wfW = 80 * scale;
         float wfH = 30 * scale;

         renderer.strokeColor(Color(0.7f, 0.7f, 0.2f, 0.9f));
         renderer.strokeWidth(1.5f * scale);
         for (int i = 0; i < 20; i++)
         {
            float t1 = (float)i / 20.0f;
            float t2 = (float)(i + 1) / 20.0f;
            float y1 = waveformSample(mShape, t1) * 0.5f * mDepth;
            float y2 = waveformSample(mShape, t2) * 0.5f * mDepth;
            renderer.line(wfX + t1 * wfW, wfY + wfH * 0.5f - y1 * wfH,
                          wfX + t2 * wfW, wfY + wfH * 0.5f - y2 * wfH);
         }
      }

      void LFOModule::setControlValue(const std::string& name, float value)
      {
         if (name == "rate")
            mRate = value;
         else if (name == "depth")
            mDepth = value;
         else if (name == "shape")
            mShape = static_cast<int>(value);
      }

      float LFOModule::getControlValue(const std::string& name) const
      {
         if (name == "rate")
            return mRate;
         if (name == "depth")
            return mDepth;
         if (name == "shape")
            return static_cast<float>(mShape);
         return 0.0f;
      }

      // ============================================================
      // TransportModule
      // ============================================================

      TransportModule::TransportModule(int id)
      : Module(id, "transport", "Transport", ModuleCategory::Other)
      {
         setSize(250, 60);
      }

      void TransportModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         float w = mWidth * scale;
         float h = mHeight * scale;

         // Transport background
         renderer.fillColor(Color(0.14f, 0.14f, 0.16f, 0.95f));
         renderer.roundedRect(screenX, screenY, w, h, 4.0f * scale);
         renderer.fill();

         renderer.strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(screenX, screenY, w, h, 4.0f * scale);
         renderer.stroke();

         // Play/Stop indicator
         float btnX = screenX + 10 * scale;
         float btnY = screenY + 15 * scale;

         if (mPlaying)
         {
            renderer.fillColor(Color(0.3f, 0.8f, 0.4f, 1.0f));
            // Draw pause bars
            renderer.rect(btnX, btnY, 4 * scale, 14 * scale);
            renderer.fill();
            renderer.rect(btnX + 7 * scale, btnY, 4 * scale, 14 * scale);
            renderer.fill();
         }
         else
         {
            renderer.fillColor(Color(0.6f, 0.6f, 0.65f, 1.0f));
            // Draw play triangle
            renderer.beginPath();
            renderer.moveTo(btnX, btnY);
            renderer.lineTo(btnX + 12 * scale, btnY + 7 * scale);
            renderer.lineTo(btnX, btnY + 14 * scale);
            renderer.closePath();
            renderer.fill();
         }

         // BPM display
         char bpmText[32];
         snprintf(bpmText, sizeof(bpmText), "%.1f BPM", mBPM);
         drawText(renderer, btnX + 25 * scale, textBaselineFromTop(renderer, screenY + 10 * scale),
                  bpmText, UITheme::kTextPrimary, 14.0f * scale);

         // Time signature
         char timeSigText[16];
         snprintf(timeSigText, sizeof(timeSigText), "%d/%d", mTimeSigTop, mTimeSigBottom);
         drawText(renderer, screenX + 140 * scale, textBaselineFromTop(renderer, screenY + 10 * scale),
                  timeSigText, UITheme::kTextSecondary, 11.0f * scale);

         // Swing
         char swingText[16];
         snprintf(swingText, sizeof(swingText), "Swing %.0f%%", mSwing * 100.0f);
         drawText(renderer, screenX + 185 * scale, textBaselineFromTop(renderer, screenY + 10 * scale),
                  swingText, UITheme::kTextSecondary, 11.0f * scale);

         const char* stateText = mPlaying ? "Playing" : "Stopped";
         drawText(renderer, screenX + 10 * scale, textBaselineFromTop(renderer, screenY + 38 * scale),
                  stateText, mPlaying ? UITheme::kAccentGreen : UITheme::kTextSecondary, 9.0f * scale);

         drawText(renderer, screenX + 70 * scale, textBaselineFromTop(renderer, screenY + 38 * scale),
                  "Space: play/stop", UITheme::kTextSecondary, 9.0f * scale);
      }

      void TransportModule::setControlValue(const std::string& name, float value)
      {
         if (name == "bpm")
            mBPM = value;
         else if (name == "swing")
            mSwing = value;
         else if (name == "playing")
            mPlaying = value > 0.5f;
         else if (name == "timesig_top")
            mTimeSigTop = static_cast<int>(value);
         else if (name == "timesig_bottom")
            mTimeSigBottom = static_cast<int>(value);
      }

      float TransportModule::getControlValue(const std::string& name) const
      {
         if (name == "bpm")
            return mBPM;
         if (name == "swing")
            return mSwing;
         if (name == "playing")
            return mPlaying ? 1.0f : 0.0f;
         return 0.0f;
      }

      // ============================================================
      // ScaleModule
      // ============================================================

      const char* const ScaleModule::kNoteNames[] = {
         "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
      };

      const char* const ScaleModule::kScaleNames[] = {
         "Major", "Minor", "Dorian", "Mixolydian", "Pentatonic", "Blues", "Chromatic"
      };

      ScaleModule::ScaleModule(int id)
      : Module(id, "scale", "Scale", ModuleCategory::Other)
      {
         setSize(180, 70);
      }

      void ScaleModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         float w = mWidth * scale;
         float h = mHeight * scale;

         // Background
         renderer.fillColor(Color(0.14f, 0.14f, 0.16f, 0.95f));
         renderer.roundedRect(screenX, screenY, w, h, 4.0f * scale);
         renderer.fill();

         renderer.strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(screenX, screenY, w, h, 4.0f * scale);
         renderer.stroke();

         // Root note + scale type
         drawText(renderer, screenX + 10 * scale, textBaselineFromTop(renderer, screenY + 6 * scale),
                  kNoteNames[mRootNote % 12], UITheme::kTextPrimary, 13.0f * scale);

         drawText(renderer, screenX + 38 * scale, textBaselineFromTop(renderer, screenY + 6 * scale),
                  kScaleNames[mScaleType % 7], UITheme::kTextSecondary, 11.0f * scale);

         // Piano keys visualization (mini)
         float keyX = screenX + 10 * scale;
         float keyY = screenY + 32 * scale;
         float keyW = 10 * scale;
         float keyH = 22 * scale;

         for (int i = 0; i < 12; i++)
         {
            bool isBlack = (i == 1 || i == 3 || i == 6 || i == 8 || i == 10);
            bool isRoot = (i == mRootNote);

            if (isRoot)
            {
               renderer.fillColor(Color(0.4f, 0.7f, 0.9f, 1.0f));
            }
            else if (isBlack)
            {
               renderer.fillColor(Color(0.15f, 0.15f, 0.17f, 1.0f));
            }
            else
            {
               renderer.fillColor(Color(0.85f, 0.85f, 0.87f, 1.0f));
            }

            renderer.rect(keyX + i * (keyW + 1), keyY, keyW, isBlack ? keyH * 0.6f : keyH);
            renderer.fill();
         }

         // Label below keys
         drawText(renderer, screenX + 10 * scale, textBaselineFromTop(renderer, keyY + keyH + 4 * scale),
                  "Root / scale", UITheme::kTextSecondary, 9.0f * scale);
      }

      void ScaleModule::setControlValue(const std::string& name, float value)
      {
         if (name == "root")
            mRootNote = static_cast<int>(value) % 12;
         else if (name == "type")
            mScaleType = static_cast<int>(value) % 7;
      }

      float ScaleModule::getControlValue(const std::string& name) const
      {
         if (name == "root")
            return static_cast<float>(mRootNote);
         if (name == "type")
            return static_cast<float>(mScaleType);
         return 0.0f;
      }

      // ============================================================
      // ModuleFactory
      // ============================================================

      ModuleFactory& ModuleFactory::instance()
      {
         static ModuleFactory factory;
         return factory;
      }

      ModuleFactory::ModuleFactory()
      {
         // Register all available module types
         mTypes.push_back({ "oscillator", "Oscillator", ModuleCategory::Synth });
         mTypes.push_back({ "gain", "Gain", ModuleCategory::AudioEffect });
         mTypes.push_back({ "output", "Output", ModuleCategory::Other });
         mTypes.push_back({ "filter", "Filter", ModuleCategory::AudioEffect });
         mTypes.push_back({ "lfo", "LFO", ModuleCategory::Modulator });
         mTypes.push_back({ "transport", "Transport", ModuleCategory::Other });
         mTypes.push_back({ "scale", "Scale", ModuleCategory::Other });
      }

      std::unique_ptr<Module> ModuleFactory::createModule(const std::string& type, int id)
      {
         if (type == "oscillator")
            return std::make_unique<OscillatorModule>(id);
         if (type == "gain")
            return std::make_unique<GainModule>(id);
         if (type == "output")
            return std::make_unique<OutputModule>(id);
         if (type == "filter")
            return std::make_unique<FilterModule>(id);
         if (type == "lfo")
            return std::make_unique<LFOModule>(id);
         if (type == "transport")
            return std::make_unique<TransportModule>(id);
         if (type == "scale")
            return std::make_unique<ScaleModule>(id);
         return nullptr;
      }

      std::vector<ModuleTypeInfo> ModuleFactory::getTypesByCategory(ModuleCategory category) const
      {
         std::vector<ModuleTypeInfo> result;
         for (const auto& t : mTypes)
         {
            if (t.category != category)
               continue;
            // Singleton modules are always present on the canvas
            if (t.type == "transport" || t.type == "scale")
               continue;
            result.push_back(t);
         }
         return result;
      }

      // ============================================================
      // ModuleCanvas
      // ============================================================

      ModuleCanvas::ModuleCanvas()
      {
         // Create persistent Transport module
         auto transport = std::make_unique<TransportModule>(mNextModuleId++);
         transport->setPosition(20, 50);
         mTransport = transport.get();
         mModules[mTransport->getId()] = std::move(transport);

         // Create persistent Scale module
         auto scaleModule = std::make_unique<ScaleModule>(mNextModuleId++);
         scaleModule->setPosition(290, 50);
         mScaleModule = scaleModule.get();
         mModules[mScaleModule->getId()] = std::move(scaleModule);

         Lock lock(mMutex);
         publishAudioGraphSnapshotLocked();
         printf("ModuleCanvas: Created with Transport and Scale modules\n");
      }

      int ModuleCanvas::createModule(const std::string& type, float x, float y)
      {
         Lock lock(mMutex);
         auto module = ModuleFactory::instance().createModule(type, mNextModuleId);
         if (!module)
         {
            printf("ModuleCanvas: Unknown module type '%s'\n", type.c_str());
            return -1;
         }

         int id = mNextModuleId++;
         module->setPosition(x, y);
         printf("ModuleCanvas: Created module '%s' (id=%d) at (%.1f, %.1f)\n",
                type.c_str(), id, x, y);
         mModules[id] = std::move(module);
         publishAudioGraphSnapshotLocked();
         return id;
      }

      void ModuleCanvas::deleteModule(int moduleId)
      {
         Lock lock(mMutex);
         // Don't allow deleting Transport or Scale
         if (mTransport && moduleId == mTransport->getId())
            return;
         if (mScaleModule && moduleId == mScaleModule->getId())
            return;

         // Remove connections involving this module
         mConnections.erase(
         std::remove_if(mConnections.begin(), mConnections.end(),
                        [moduleId](const Connection& c)
                        {
                           return c.sourceModuleId == moduleId || c.destModuleId == moduleId;
                        }),
         mConnections.end());

         mModules.erase(moduleId);
         printf("ModuleCanvas: Deleted module %d\n", moduleId);
         publishAudioGraphSnapshotLocked();
      }

      Module* ModuleCanvas::getModule(int moduleId)
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it != mModules.end())
            return it->second.get();
         return nullptr;
      }

      int ModuleCanvas::findFirstModuleOfType(const std::string& type) const
      {
         Lock lock(mMutex);
         for (const auto& [id, module] : mModules)
         {
            if (module->getType() == type)
               return id;
         }
         return -1;
      }

      bool ModuleCanvas::setModuleControlValue(int moduleId, const std::string& name, float value)
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return false;
         it->second->setControlValue(name, value);
         publishAudioGraphSnapshotLocked();
         return true;
      }

      bool ModuleCanvas::getModuleControlValue(int moduleId, const std::string& name, float& value) const
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return false;
         value = it->second->getControlValue(name);
         return true;
      }

      void ModuleCanvas::connectModules(int sourceId, int sourcePort, int destId, int destPort)
      {
         Lock lock(mMutex);
         if (!portsAreCompatible(sourceId, sourcePort, destId, destPort))
         {
            printf("ModuleCanvas: Rejected incompatible connection %d:%d -> %d:%d\n",
                   sourceId, sourcePort, destId, destPort);
            return;
         }
         for (const auto& existing : mConnections)
         {
            if (existing.sourceModuleId == sourceId && existing.sourcePortIndex == sourcePort &&
                existing.destModuleId == destId && existing.destPortIndex == destPort)
               return;
         }
         Connection conn;
         conn.sourceModuleId = sourceId;
         conn.sourcePortIndex = sourcePort;
         conn.destModuleId = destId;
         conn.destPortIndex = destPort;

         // Color based on port type
         Module* srcMod = getModule(sourceId);
         if (srcMod && sourcePort < static_cast<int>(srcMod->getOutputs().size()))
         {
            switch (srcMod->getOutputs()[sourcePort].type)
            {
               case PortType::Audio:
                  conn.color = Color(0.3f, 0.7f, 0.9f, 0.9f);
                  break;
               case PortType::Note:
                  conn.color = Color(0.9f, 0.7f, 0.3f, 0.9f);
                  break;
               case PortType::Pulse:
                  conn.color = Color(0.9f, 0.3f, 0.3f, 0.9f);
                  break;
               case PortType::Modulation:
                  conn.color = Color(0.5f, 0.9f, 0.4f, 0.9f);
                  break;
            }
         }
         else
         {
            conn.color = Color(0.5f, 0.5f, 0.55f, 0.9f);
         }

         mConnections.push_back(conn);
         printf("ModuleCanvas: Connected %d:%d -> %d:%d\n", sourceId, sourcePort, destId, destPort);
         publishAudioGraphSnapshotLocked();
      }

      void ModuleCanvas::disconnectModules(int sourceId, int destId)
      {
         Lock lock(mMutex);
         mConnections.erase(
         std::remove_if(mConnections.begin(), mConnections.end(),
                        [sourceId, destId](const Connection& c)
                        {
                           return c.sourceModuleId == sourceId && c.destModuleId == destId;
                        }),
         mConnections.end());
         publishAudioGraphSnapshotLocked();
      }

      std::shared_ptr<const ModuleCanvas::AudioGraphSnapshot> ModuleCanvas::getAudioGraphSnapshot() const
      {
         return std::atomic_load_explicit(&mPublishedAudioGraph, std::memory_order_acquire);
      }

      void ModuleCanvas::buildAudioGraphSnapshotLocked(AudioGraphSnapshot& snapshot) const
      {
         snapshot.transportPlaying = mTransport && mTransport->isPlaying();
         snapshot.transportBPM = mTransport ? mTransport->getBPM() : 120.0f;
         snapshot.nodes.clear();
         snapshot.connections.clear();
         snapshot.nodes.reserve(mModules.size());
         snapshot.connections.reserve(mConnections.size());

         for (const auto& [id, module] : mModules)
         {
            AudioGraphNode node;
            node.id = id;
            node.type = module->getType();
            node.enabled = module->isEnabled();

            if (node.type == "oscillator")
            {
               node.frequency = module->getControlValue("frequency");
               node.volume = module->getControlValue("volume");
               node.waveform = static_cast<int>(module->getControlValue("waveform"));
            }
            else if (node.type == "gain")
            {
               node.gain = module->getControlValue("gain");
            }
            else if (node.type == "filter")
            {
               node.cutoff = module->getControlValue("cutoff");
               node.resonance = module->getControlValue("resonance");
               node.filterType = static_cast<int>(module->getControlValue("type"));
            }
            else if (node.type == "lfo")
            {
               node.lfoRate = module->getControlValue("rate");
               node.lfoDepth = module->getControlValue("depth");
               node.lfoShape = static_cast<int>(module->getControlValue("shape"));
            }

            snapshot.nodes.push_back(node);
         }

         for (const auto& conn : mConnections)
         {
            auto srcIt = mModules.find(conn.sourceModuleId);
            auto dstIt = mModules.find(conn.destModuleId);
            if (srcIt == mModules.end() || dstIt == mModules.end())
               continue;

            const auto& outputs = srcIt->second->getOutputs();
            const auto& inputs = dstIt->second->getInputs();
            if (conn.sourcePortIndex < 0 || conn.destPortIndex < 0 ||
                conn.sourcePortIndex >= static_cast<int>(outputs.size()) ||
                conn.destPortIndex >= static_cast<int>(inputs.size()))
               continue;

            AudioGraphConnection audioConn;
            audioConn.sourceModuleId = conn.sourceModuleId;
            audioConn.sourcePortIndex = conn.sourcePortIndex;
            audioConn.destModuleId = conn.destModuleId;
            audioConn.destPortIndex = conn.destPortIndex;
            audioConn.sourcePortType = outputs[conn.sourcePortIndex].type;
            audioConn.destPortType = inputs[conn.destPortIndex].type;
            snapshot.connections.push_back(audioConn);
         }
      }

      void ModuleCanvas::publishAudioGraphSnapshotLocked()
      {
         AudioGraphSnapshot snapshot;
         buildAudioGraphSnapshotLocked(snapshot);
         std::atomic_store_explicit(
            &mPublishedAudioGraph,
            std::make_shared<const AudioGraphSnapshot>(std::move(snapshot)),
            std::memory_order_release);
      }

      bool ModuleCanvas::portsAreCompatible(int sourceId, int sourcePort, int destId, int destPort) const
      {
         auto sourceIt = mModules.find(sourceId);
         auto destIt = mModules.find(destId);
         if (sourceIt == mModules.end() || destIt == mModules.end() || sourceId == destId ||
             sourcePort < 0 || destPort < 0)
            return false;
         const auto& outputs = sourceIt->second->getOutputs();
         const auto& inputs = destIt->second->getInputs();
         return sourcePort < static_cast<int>(outputs.size()) && destPort < static_cast<int>(inputs.size()) &&
                outputs[sourcePort].type == inputs[destPort].type;
      }

      bool ModuleCanvas::findPortAt(float worldX, float worldY, bool output, int& moduleId, int& portIndex) const
      {
         const float hitRadius = Module::kPortRadius + 4.0f;
         for (const auto& [id, module] : mModules)
         {
            const auto& ports = output ? module->getOutputs() : module->getInputs();
            const float portX = module->getX() + (output ? module->getWidth() : 0.0f);
            for (size_t index = 0; index < ports.size(); ++index)
            {
               const float portY = module->getY() + Module::kTitleBarHeight + 10.0f + index * 15.0f;
               const float dx = worldX - portX;
               const float dy = worldY - portY;
               if (dx * dx + dy * dy <= hitRadius * hitRadius)
               {
                  moduleId = id;
                  portIndex = static_cast<int>(index);
                  return true;
               }
            }
         }
         return false;
      }

      bool ModuleCanvas::removeConnectionAt(float screenX, float screenY)
      {
         const float canvasTop = kTitleBarHeight;
         const float threshold = 8.0f;
         for (auto it = mConnections.begin(); it != mConnections.end(); ++it)
         {
            auto sourceIt = mModules.find(it->sourceModuleId);
            auto destIt = mModules.find(it->destModuleId);
            if (sourceIt == mModules.end() || destIt == mModules.end())
               continue;
            const Module& source = *sourceIt->second;
            const Module& dest = *destIt->second;
            const float x1 = (source.getX() + source.getWidth() + mOffsetX) * mScale;
            const float y1 = (source.getY() + Module::kTitleBarHeight + 10.0f + it->sourcePortIndex * 15.0f + mOffsetY) * mScale + canvasTop;
            const float x2 = (dest.getX() + mOffsetX) * mScale;
            const float y2 = (dest.getY() + Module::kTitleBarHeight + 10.0f + it->destPortIndex * 15.0f + mOffsetY) * mScale + canvasTop;
            const float dx = x2 - x1;
            const float dy = y2 - y1;
            const float lengthSquared = dx * dx + dy * dy;
            const float t = lengthSquared > 0.0f
                            ? std::max(0.0f, std::min(1.0f, ((screenX - x1) * dx + (screenY - y1) * dy) / lengthSquared))
                            : 0.0f;
            const float px = x1 + t * dx;
            const float py = y1 + t * dy;
            const float distanceX = screenX - px;
            const float distanceY = screenY - py;
            if (distanceX * distanceX + distanceY * distanceY <= threshold * threshold)
            {
               mConnections.erase(it);
               return true;
            }
         }
         return false;
      }

      ModuleCanvas::AudioGraphSnapshot ModuleCanvas::createAudioGraphSnapshot() const
      {
         Lock lock(mMutex);
         AudioGraphSnapshot snapshot;
         buildAudioGraphSnapshotLocked(snapshot);
         return snapshot;
      }

      ModuleCanvas::StateSnapshot ModuleCanvas::createStateSnapshot() const
      {
         Lock lock(mMutex);

         StateSnapshot snapshot;
         snapshot.transportBPM = mTransport ? mTransport->getBPM() : 120.0f;
         snapshot.transportPlaying = mTransport && mTransport->isPlaying();
         snapshot.offsetX = mOffsetX;
         snapshot.offsetY = mOffsetY;
         snapshot.scale = mScale;
         snapshot.modules.reserve(mModules.size());
         snapshot.connections.reserve(mConnections.size());

         for (const auto& [id, module] : mModules)
         {
            StateModule stateModule;
            stateModule.id = id;
            stateModule.type = module->getType();
            stateModule.x = module->getX();
            stateModule.y = module->getY();
            stateModule.minimized = module->isMinimized();
            stateModule.enabled = module->isEnabled();

            for (const auto& controlName : serializableControlsForType(stateModule.type))
               stateModule.controls[controlName] = module->getControlValue(controlName);

            snapshot.modules.push_back(stateModule);
         }

         for (const auto& conn : mConnections)
         {
            StateConnection stateConn;
            stateConn.sourceModuleId = conn.sourceModuleId;
            stateConn.sourcePortIndex = conn.sourcePortIndex;
            stateConn.destModuleId = conn.destModuleId;
            stateConn.destPortIndex = conn.destPortIndex;
            snapshot.connections.push_back(stateConn);
         }

         return snapshot;
      }

      bool ModuleCanvas::applyStateSnapshot(const StateSnapshot& snapshot)
      {
         Lock lock(mMutex);
         std::unordered_map<int, int> idMap;

         clearUserModules();
         setViewTransform(snapshot.offsetX, snapshot.offsetY, snapshot.scale);

         if (mTransport)
         {
            mTransport->setBPM(snapshot.transportBPM);
            mTransport->setPlaying(snapshot.transportPlaying);
         }

         for (const auto& stateModule : snapshot.modules)
         {
            Module* module = nullptr;
            int newId = -1;

            if (stateModule.type == "transport")
            {
               module = mTransport;
               newId = module ? module->getId() : -1;
            }
            else if (stateModule.type == "scale")
            {
               module = mScaleModule;
               newId = module ? module->getId() : -1;
            }
            else
            {
               newId = createModule(stateModule.type, stateModule.x, stateModule.y);
               module = getModule(newId);
            }

            if (!module || newId < 0)
               continue;

            idMap[stateModule.id] = newId;
            module->setPosition(stateModule.x, stateModule.y);
            module->setMinimized(stateModule.minimized);
            module->setEnabled(stateModule.enabled);

            for (const auto& [name, value] : stateModule.controls)
               module->setControlValue(name, value);
         }

         for (const auto& stateConn : snapshot.connections)
         {
            auto srcIt = idMap.find(stateConn.sourceModuleId);
            auto dstIt = idMap.find(stateConn.destModuleId);
            if (srcIt == idMap.end() || dstIt == idMap.end())
               continue;
            connectModules(srcIt->second, stateConn.sourcePortIndex, dstIt->second, stateConn.destPortIndex);
         }

         publishAudioGraphSnapshotLocked();
         return true;
      }

      void ModuleCanvas::zoom(float factor, float centerX, float centerY)
      {
         Lock lock(mMutex);
         float worldCenterX = screenToWorldX(centerX);
         float worldCenterY = screenToWorldY(centerY);

         mScale *= factor;
         mScale = std::max(0.25f, std::min(4.0f, mScale));

         // Adjust offset to keep the zoom centered
         mOffsetX = centerX / mScale - worldCenterX;
         mOffsetY = centerY / mScale - worldCenterY;
      }

      float ModuleCanvas::screenToWorldX(float screenX) const
      {
         return screenX / mScale - mOffsetX;
      }

      float ModuleCanvas::screenToWorldY(float screenY) const
      {
         return screenY / mScale - mOffsetY;
      }

      Color ModuleCanvas::getCategoryColor(ModuleCategory cat)
      {
         switch (cat)
         {
            case ModuleCategory::Instrument:
               return Color(0.2f, 0.5f, 0.8f, 1.0f);
            case ModuleCategory::NoteEffect:
               return Color(0.6f, 0.3f, 0.7f, 1.0f);
            case ModuleCategory::Synth:
               return Color(0.3f, 0.7f, 0.5f, 1.0f);
            case ModuleCategory::AudioEffect:
               return Color(0.8f, 0.5f, 0.2f, 1.0f);
            case ModuleCategory::Modulator:
               return Color(0.7f, 0.7f, 0.2f, 1.0f);
            case ModuleCategory::Pulse:
               return Color(0.8f, 0.3f, 0.3f, 1.0f);
            default:
               return Color(0.4f, 0.4f, 0.45f, 1.0f);
         }
      }

      void ModuleCanvas::renderTitleBar(Renderer2D& renderer, int viewWidth)
      {
         float w = static_cast<float>(viewWidth);

         // Title bar background
         renderer.fillColor(Color(0.1f, 0.1f, 0.12f, 0.95f));
         renderer.rect(0, 0, w, kTitleBarHeight);
         renderer.fill();

         // Title bar bottom border
         renderer.strokeColor(Color(0.3f, 0.3f, 0.35f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.line(0, kTitleBarHeight, w, kTitleBarHeight);

         // Logo / title
         drawText(renderer, 10.0f, textBaselineFromTop(renderer, 8.0f),
                  "BespokeSynth", UITheme::kTextPrimary, 14.0f);

         float btnX = 140.0f;
         float btnW = 112.0f;
         float btnH = 24.0f;
         float btnY = 8.0f;
         renderer.fillColor(mSpawnMenuOpen ? Color(0.25f, 0.25f, 0.30f, 1.0f)
                                           : Color(0.18f, 0.18f, 0.20f, 1.0f));
         renderer.roundedRect(btnX, btnY, btnW, btnH, 3.0f);
         renderer.fill();
         renderer.strokeColor(Color(0.30f, 0.70f, 0.50f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(btnX, btnY, btnW, btnH, 3.0f);
         renderer.stroke();
         drawText(renderer, btnX + 8.0f, textBaselineFromTop(renderer, btnY + 4.0f),
                  "Add module  /", UITheme::kTextSecondary, 11.0f);
      }

      void ModuleCanvas::renderSpawnMenu(Renderer2D& renderer, int viewWidth, int viewHeight)
      {
         // Keep the palette native so it shares the canvas transform and Bespoke visual language.
         // TypeScript only forwards browser-only input; a DOM overlay would be easier to iterate
         // on, but would need duplicate placement, focus, and renderer-backend coordination.
         if (!mSpawnMenuOpen)
            return;

         const auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
         constexpr float kMenuWidth = 260.0f;
         constexpr float kSearchHeight = 29.0f;
         constexpr float kCategoryHeight = 25.0f;
         constexpr float kRowHeight = 24.0f;
         constexpr int kMaxRows = 8;
         const int rows = std::min(static_cast<int>(types.size()), kMaxRows);
         const float menuHeight = kSearchHeight + kCategoryHeight + 8.0f + rows * kRowHeight + 10.0f;
         const float menuX = std::max(6.0f, std::min(mSpawnMenuX, static_cast<float>(viewWidth) - kMenuWidth - 6.0f));
         const float menuY = std::max(kTitleBarHeight + 4.0f,
                                      std::min(mSpawnMenuY, static_cast<float>(viewHeight) - menuHeight - 6.0f));
         mSpawnMenuRenderX = menuX;
         mSpawnMenuRenderY = menuY;

         renderer.fillColor(Color(0.12f, 0.12f, 0.14f, 0.98f));
         renderer.roundedRect(menuX, menuY, kMenuWidth, menuHeight, 5.0f);
         renderer.fill();
         renderer.strokeColor(Color(0.38f, 0.38f, 0.44f, 1.0f));
         renderer.strokeWidth(1.0f);
         renderer.roundedRect(menuX, menuY, kMenuWidth, menuHeight, 5.0f);
         renderer.stroke();

         const std::string searchLabel = mSpawnMenuSearch.empty() ? "Search modules..." : mSpawnMenuSearch;
         drawText(renderer, menuX + 10.0f, textBaselineFromTop(renderer, menuY + 7.0f),
                  searchLabel.c_str(), mSpawnMenuSearch.empty() ? UITheme::kTextSecondary : UITheme::kTextPrimary, 12.0f);
         renderer.strokeColor(Color(0.28f, 0.28f, 0.33f, 1.0f));
         renderer.line(menuX + 8.0f, menuY + kSearchHeight, menuX + kMenuWidth - 8.0f, menuY + kSearchHeight);

         const char* categories[] = { "All", "Synth", "FX", "Mod", "Other" };
         const int categoryValues[] = { -1, static_cast<int>(ModuleCategory::Synth), static_cast<int>(ModuleCategory::AudioEffect),
                                        static_cast<int>(ModuleCategory::Modulator), static_cast<int>(ModuleCategory::Other) };
         for (int i = 0; i < 5; ++i)
         {
            const float chipX = menuX + 8.0f + i * 48.5f;
            if (mSpawnMenuCategory == categoryValues[i])
            {
               renderer.fillColor(Color(0.25f, 0.35f, 0.30f, 1.0f));
               renderer.roundedRect(chipX, menuY + kSearchHeight + 3.0f, 44.0f, 18.0f, 3.0f);
               renderer.fill();
            }
            drawText(renderer, chipX + 5.0f, textBaselineFromTop(renderer, menuY + kSearchHeight + 6.0f),
                     categories[i], UITheme::kTextSecondary, 10.0f);
         }

         const float rowsY = menuY + kSearchHeight + kCategoryHeight + 2.0f;
         if (types.empty())
         {
            drawText(renderer, menuX + 10.0f, textBaselineFromTop(renderer, rowsY + 5.0f),
                     "No matching modules", UITheme::kTextSecondary, 11.0f);
            return;
         }
         for (int i = 0; i < rows; ++i)
         {
            const float rowY = rowsY + i * kRowHeight;
            if (i == mSpawnMenuSelectedIndex)
            {
               renderer.fillColor(Color(0.24f, 0.30f, 0.34f, 1.0f));
               renderer.rect(menuX + 5.0f, rowY, kMenuWidth - 10.0f, kRowHeight);
               renderer.fill();
            }
            drawText(renderer, menuX + 11.0f, textBaselineFromTop(renderer, rowY + 4.0f),
                     types[i].displayName.c_str(), UITheme::kTextPrimary, 12.0f);
         }
      }

      void ModuleCanvas::render(Renderer2D& renderer, int viewWidth, int viewHeight)
      {
         Lock lock(mMutex);
         float canvasTop = kTitleBarHeight;
         float canvasHeight = static_cast<float>(viewHeight) - canvasTop;

         // Canvas background with grid
         renderer.fillColor(Color(0.09f, 0.09f, 0.1f, 1.0f));
         renderer.rect(0, canvasTop, static_cast<float>(viewWidth), canvasHeight);
         renderer.fill();

         // Draw grid dots
         float gridSize = 50.0f * mScale;
         float startGridX = fmodf(mOffsetX * mScale, gridSize);
         float startGridY = fmodf(mOffsetY * mScale + canvasTop, gridSize);

         renderer.fillColor(Color(0.2f, 0.2f, 0.22f, 0.5f));
         for (float gx = startGridX; gx < viewWidth; gx += gridSize)
         {
            for (float gy = canvasTop + startGridY; gy < viewHeight; gy += gridSize)
            {
               renderer.circle(gx, gy, 1.0f);
               renderer.fill();
            }
         }

         // Draw connections (cables)
         for (const auto& conn : mConnections)
         {
            Module* srcMod = getModule(conn.sourceModuleId);
            Module* dstMod = getModule(conn.destModuleId);
            if (!srcMod || !dstMod)
               continue;

            // Calculate port positions
            float srcX = (srcMod->getX() + srcMod->getWidth() + mOffsetX) * mScale;
            float srcY = (srcMod->getY() + Module::kTitleBarHeight + 10 +
                          conn.sourcePortIndex * 15 + mOffsetY) *
                         mScale +
                         canvasTop;
            float dstX = (dstMod->getX() + mOffsetX) * mScale;
            float dstY = (dstMod->getY() + Module::kTitleBarHeight + 10 +
                          conn.destPortIndex * 15 + mOffsetY) *
                         mScale +
                         canvasTop;

            renderer.drawCableWithSag(srcX, srcY, dstX, dstY, conn.color, 2.5f * mScale, 0.2f);
         }

         // Draw in-progress connection
         if (mIsConnecting)
         {
            Module* srcMod = getModule(mConnectSourceId);
            if (srcMod)
            {
               float srcX = (srcMod->getX() + srcMod->getWidth() + mOffsetX) * mScale;
               float srcY = (srcMod->getY() + Module::kTitleBarHeight + 10 +
                             mConnectSourcePort * 15 + mOffsetY) *
                            mScale +
                            canvasTop;
               const Color previewColor = mConnectTargetCompatible
                                          ? Color(0.7f, 0.7f, 0.75f, 0.75f)
                                          : Color(0.95f, 0.2f, 0.2f, 0.85f);
               renderer.drawCableWithSag(srcX, srcY, mConnectEndX, mConnectEndY,
                                         previewColor, 2.0f, 0.15f);
            }
         }

         // Draw modules
         for (auto& [id, module] : mModules)
         {
            module->render(renderer, mOffsetX, mOffsetY + canvasTop / mScale, mScale);
         }

         // Render title bar on top
         renderTitleBar(renderer, viewWidth);
         renderSpawnMenu(renderer, viewWidth, viewHeight);

         // Docked live analyzer: its data is copied from the audio callback's lock-free ring.
         std::array<float, 256> waveform{};
         std::array<float, AudioAnalysis::kSpectrumBins> spectrum{};
         AudioAnalysis::copyLatest(waveform.data(), static_cast<int>(waveform.size()));
         AudioAnalysis::computeSpectrum(spectrum.data(), static_cast<int>(spectrum.size()));
         const float analyzerX = static_cast<float>(viewWidth) - 210.0f;
         const float analyzerY = static_cast<float>(viewHeight) - 135.0f;
         renderer.drawPanel(analyzerX, analyzerY, 200.0f, 105.0f, true);
         drawText(renderer, analyzerX + 8.0f, textBaselineFromTop(renderer, analyzerY + 5.0f),
                  "ANALYZER", UITheme::kTextSecondary, 9.0f);
         renderer.drawWaveform(analyzerX + 8.0f, analyzerY + 20.0f, 184.0f, 34.0f,
                               waveform.data(), static_cast<int>(waveform.size()), false);
         renderer.drawSpectrum(analyzerX + 8.0f, analyzerY + 61.0f, 184.0f, 35.0f,
                               spectrum.data(), static_cast<int>(spectrum.size()));

         // Draw zoom indicator (right-aligned)
         renderer.fontSize(10.0f);
         char zoomText[32];
         snprintf(zoomText, sizeof(zoomText), "%.0f%%", mScale * 100.0f);
         const float zoomW = renderer.textWidth(zoomText);
         drawText(renderer, static_cast<float>(viewWidth) - zoomW - 10.0f,
                  textBaselineFromTop(renderer, static_cast<float>(viewHeight) - 18.0f),
                  zoomText, Color(0.55f, 0.55f, 0.60f, 0.85f), 10.0f);

         // Status line
         const bool playing = mTransport && mTransport->isPlaying();
         const float bpm = mTransport ? mTransport->getBPM() : 120.0f;
         char countText[128];
         snprintf(countText, sizeof(countText),
                  "Modules: %d | Cables: %d | %s | %.1f BPM | Tab: demo panels",
                  static_cast<int>(mModules.size()),
                  static_cast<int>(mConnections.size()),
                  playing ? "Playing" : "Stopped",
                  bpm);
         drawText(renderer, 10.0f, textBaselineFromTop(renderer, static_cast<float>(viewHeight) - 18.0f),
                  countText, UITheme::kTextSecondary, 10.0f);
      }

      void ModuleCanvas::setOutputLevel(float level)
      {
         Lock lock(mMutex);
         for (auto& [id, module] : mModules)
         {
            if (module->getType() == "output")
            {
               module->setControlValue("level", level);
               break;
            }
         }
      }

      void ModuleCanvas::clearUserModules()
      {
         Lock lock(mMutex);
         std::vector<int> toDelete;
         for (const auto& [id, module] : mModules)
         {
            if (mTransport && id == mTransport->getId())
               continue;
            if (mScaleModule && id == mScaleModule->getId())
               continue;
            toDelete.push_back(id);
         }
         for (int id : toDelete)
            deleteModule(id);
      }

      void ModuleCanvas::setViewTransform(float offsetX, float offsetY, float scale)
      {
         Lock lock(mMutex);
         mOffsetX = offsetX;
         mOffsetY = offsetY;
         mScale = std::max(0.25f, std::min(4.0f, scale));
      }

      void ModuleCanvas::setupCanonicalRenderTestScene()
      {
         Lock lock(mMutex);
         clearUserModules();
         setViewTransform(0.0f, 80.0f, 1.0f);

         if (mTransport)
         {
            mTransport->setPosition(20.0f, 50.0f);
            mTransport->setBPM(128.0f);
            mTransport->setPlaying(false);
            mTransport->setControlValue("swing", 0.15f);
         }
         if (mScaleModule)
         {
            mScaleModule->setPosition(290.0f, 50.0f);
            mScaleModule->setControlValue("root", 0.0f);
            mScaleModule->setControlValue("type", 0.0f);
         }

         const int oscId = createModule("oscillator", 80.0f, 160.0f);
         const int filterId = createModule("filter", 280.0f, 170.0f);
         const int gainId = createModule("gain", 470.0f, 180.0f);
         const int lfoId = createModule("lfo", 80.0f, 310.0f);
         const int outputId = createModule("output", 650.0f, 190.0f);

         if (auto* osc = getModule(oscId))
         {
            osc->setControlValue("frequency", 440.0f);
            osc->setControlValue("volume", 0.75f);
            osc->setControlValue("waveform", 0.0f);
         }
         if (auto* filter = getModule(filterId))
         {
            filter->setControlValue("cutoff", 1800.0f);
            filter->setControlValue("resonance", 0.65f);
            filter->setControlValue("type", 0.0f);
         }
         if (auto* gain = getModule(gainId))
            gain->setControlValue("gain", 0.7f);
         if (auto* lfo = getModule(lfoId))
         {
            lfo->setControlValue("rate", 2.5f);
            lfo->setControlValue("depth", 0.6f);
            lfo->setControlValue("shape", 0.0f);
         }

         if (oscId > 0 && filterId > 0)
            connectModules(oscId, 0, filterId, 0);
         if (filterId > 0 && gainId > 0)
            connectModules(filterId, 0, gainId, 0);
         if (gainId > 0 && outputId > 0)
            connectModules(gainId, 0, outputId, 0);
         if (lfoId > 0 && filterId > 0)
            connectModules(lfoId, 0, filterId, 1);

         publishAudioGraphSnapshotLocked();
      }

      void ModuleCanvas::onMouseDown(float x, float y, int button)
      {
         Lock lock(mMutex);
         mLastMouseX = x;
         mLastMouseY = y;

         // The title-bar button is a convenient discoverable alternative to right-click.
         if (y < kTitleBarHeight)
         {
            float btnX = 140.0f;
            float btnW = 112.0f;
            float btnH = 24.0f;
            float btnTop = 8.0f;
            if (x >= btnX && x <= btnX + btnW && y >= btnTop && y <= btnTop + btnH)
            {
               if (mSpawnMenuOpen)
                  closeSpawnMenu();
               else
                  openSpawnMenu(x, kTitleBarHeight + 4.0f);
               return;
            }
            if (mSpawnMenuOpen)
               closeSpawnMenu();
            return;
         }

         if (mSpawnMenuOpen)
         {
            constexpr float kMenuWidth = 260.0f;
            constexpr float kSearchHeight = 29.0f;
            constexpr float kCategoryHeight = 25.0f;
            constexpr float kRowHeight = 24.0f;
            const auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
            const int rows = std::min(static_cast<int>(types.size()), 8);
            const float menuHeight = kSearchHeight + kCategoryHeight + 8.0f + rows * kRowHeight + 10.0f;
            const float menuX = mSpawnMenuRenderX;
            const float menuY = mSpawnMenuRenderY;

            if (x >= menuX && x <= menuX + kMenuWidth && y >= menuY && y <= menuY + menuHeight)
            {
               const int categoryValues[] = { -1, static_cast<int>(ModuleCategory::Synth), static_cast<int>(ModuleCategory::AudioEffect),
                                              static_cast<int>(ModuleCategory::Modulator), static_cast<int>(ModuleCategory::Other) };
               const float categoryY = menuY + kSearchHeight + 3.0f;
               if (y >= categoryY && y <= categoryY + 18.0f)
               {
                  const int chip = static_cast<int>((x - menuX - 8.0f) / 48.5f);
                  if (chip >= 0 && chip < 5)
                  {
                     mSpawnMenuCategory = categoryValues[chip];
                     mSpawnMenuSelectedIndex = 0;
                  }
                  return;
               }
               const float rowsY = menuY + kSearchHeight + kCategoryHeight + 2.0f;
               const int row = static_cast<int>((y - rowsY) / kRowHeight);
               if (row >= 0 && row < static_cast<int>(types.size()))
               {
                  const float worldX = screenToWorldX(mSpawnMenuX);
                  const float worldY = screenToWorldY(mSpawnMenuY - kTitleBarHeight);
                  createModule(types[row].type, worldX, worldY);
                  closeSpawnMenu();
                  return;
               }
               return;
            }
            closeSpawnMenu();
            return;
         }

         // Convert to world coordinates for module hit testing
         float worldX = screenToWorldX(x);
         float worldY = screenToWorldY(y - kTitleBarHeight);

         if (button == 2)
         {
            if (removeConnectionAt(x, y))
            {
               publishAudioGraphSnapshotLocked();
               return;
            }
            bool moduleAtCursor = false;
            for (const auto& [id, module] : mModules)
               moduleAtCursor = moduleAtCursor || module->hitTest(worldX, worldY);
            if (!moduleAtCursor)
            {
               openSpawnMenu(x, y);
               return;
            }
         }

         if (button == 0)
         {
            int sourceId = -1;
            int sourcePort = -1;
            if (findPortAt(worldX, worldY, true, sourceId, sourcePort))
            {
               mIsConnecting = true;
               mConnectSourceId = sourceId;
               mConnectSourcePort = sourcePort;
               mConnectEndX = x;
               mConnectEndY = y;
               mConnectTargetCompatible = true;
               return;
            }
         }

         // Transport play/stop button
         if (mTransport && mTransport->hitTest(worldX, worldY))
         {
            const float localX = worldX - mTransport->getX();
            const float localY = worldY - mTransport->getY();
            if (localX >= 10.0f && localX <= 30.0f && localY >= 15.0f && localY <= 29.0f)
            {
               mTransport->setPlaying(!mTransport->isPlaying());
               publishAudioGraphSnapshotLocked();
               return;
            }
         }

         // In-module controls (sliders, etc.) take priority over dragging
         if (button == 0)
         {
            for (auto& [id, module] : mModules)
            {
               if (module->hitTest(worldX, worldY) && module->handleMouseDown(worldX, worldY))
               {
                  mControlModuleId = id;
                  return;
               }
            }
         }

         // Check module title bar for dragging
         for (auto& [id, module] : mModules)
         {
            if (module->hitTitleBar(worldX, worldY))
            {
               mDraggedModuleId = id;
               return;
            }
         }

         // Right-click for context menu, middle-click for pan
         if (button == 1 || button == 2)
         {
            mIsPanning = true;
            mPanStartX = x;
            mPanStartY = y;
            return;
         }

         // Nothing hit - start panning with left click on empty space
         mIsPanning = true;
         mPanStartX = x;
         mPanStartY = y;
      }

      void ModuleCanvas::onMouseUp(float x, float y, int button)
      {
         Lock lock(mMutex);
         if (mIsConnecting && button == 0)
         {
            const float worldX = screenToWorldX(x);
            const float worldY = screenToWorldY(y - kTitleBarHeight);
            int destId = -1;
            int destPort = -1;
            if (findPortAt(worldX, worldY, false, destId, destPort) &&
                portsAreCompatible(mConnectSourceId, mConnectSourcePort, destId, destPort))
               connectModules(mConnectSourceId, mConnectSourcePort, destId, destPort);
         }
         if (mControlModuleId >= 0)
         {
            Module* mod = getModule(mControlModuleId);
            if (mod)
               mod->handleMouseUp();
            publishAudioGraphSnapshotLocked();
         }
         mControlModuleId = -1;
         mDraggedModuleId = -1;
         mIsPanning = false;
         mIsConnecting = false;
         mConnectSourceId = -1;
         mConnectSourcePort = -1;
      }

      void ModuleCanvas::onMouseMove(float x, float y, float prevX, float prevY)
      {
         Lock lock(mMutex);
         mLastMouseX = x;
         mLastMouseY = y;
         if (mControlModuleId >= 0)
         {
            Module* mod = getModule(mControlModuleId);
            if (mod)
            {
               float worldX = screenToWorldX(x);
               float worldY = screenToWorldY(y - kTitleBarHeight);
               float dx = (x - prevX) / mScale;
               float dy = (y - prevY) / mScale;
               mod->handleMouseDrag(worldX, worldY, dx, dy);
               publishAudioGraphSnapshotLocked();
            }
         }
         else if (mDraggedModuleId >= 0)
         {
            Module* mod = getModule(mDraggedModuleId);
            if (mod)
            {
               float dx = (x - prevX) / mScale;
               float dy = (y - prevY) / mScale;
               mod->setPosition(mod->getX() + dx, mod->getY() + dy);
            }
         }
         else if (mIsPanning)
         {
            float dx = (x - prevX) / mScale;
            float dy = (y - prevY) / mScale;
            mOffsetX += dx;
            mOffsetY += dy;
         }

         if (mIsConnecting)
         {
            mConnectEndX = x;
            mConnectEndY = y;
            const float worldX = screenToWorldX(x);
            const float worldY = screenToWorldY(y - kTitleBarHeight);
            int destId = -1;
            int destPort = -1;
            mConnectTargetCompatible = !findPortAt(worldX, worldY, false, destId, destPort) ||
                                       portsAreCompatible(mConnectSourceId, mConnectSourcePort, destId, destPort);
         }
      }

      void ModuleCanvas::onMouseWheel(float deltaX, float deltaY, float mouseX, float mouseY)
      {
         Lock lock(mMutex);
         (void)deltaX;
         float zoomFactor = 1.0f - deltaY * 0.001f;
         zoom(zoomFactor, mouseX, mouseY);
      }

      void ModuleCanvas::onKeyDown(int keyCode, int modifiers)
      {
         Lock lock(mMutex);
         static const int kKeyDelete = 46;
         static const int kKeyBackspace = 8;
         static const int kKeySpace = 32;
         static const int kKeyEscape = 27;
         static const int kKeyEnter = 13;
         static const int kKeyUp = 38;
         static const int kKeyDown = 40;
         static const int kKeySlash = 191;
         static const int kKeyK = 75;

         if (!mSpawnMenuOpen && ((keyCode == kKeySlash || keyCode == '/') || ((modifiers & 4) != 0 && keyCode == kKeyK)))
         {
            openSpawnMenu(mLastMouseX, std::max(mLastMouseY, kTitleBarHeight + 4.0f));
            return;
         }

         if (mSpawnMenuOpen)
         {
            auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
            if (keyCode == kKeyEscape)
            {
               closeSpawnMenu();
               return;
            }
            if (keyCode == kKeyBackspace)
            {
               if (!mSpawnMenuSearch.empty())
                  mSpawnMenuSearch.pop_back();
               mSpawnMenuSelectedIndex = 0;
               return;
            }
            if (keyCode == kKeyUp && !types.empty())
            {
               mSpawnMenuSelectedIndex = (mSpawnMenuSelectedIndex + static_cast<int>(types.size()) - 1) % static_cast<int>(types.size());
               return;
            }
            if (keyCode == kKeyDown && !types.empty())
            {
               mSpawnMenuSelectedIndex = (mSpawnMenuSelectedIndex + 1) % static_cast<int>(types.size());
               return;
            }
            if (keyCode == kKeyEnter && !types.empty())
            {
               const int selected = std::min(mSpawnMenuSelectedIndex, static_cast<int>(types.size()) - 1);
               createModule(types[selected].type, screenToWorldX(mSpawnMenuX),
                            screenToWorldY(mSpawnMenuY - kTitleBarHeight));
               closeSpawnMenu();
               return;
            }
            if ((modifiers & (2 | 4 | 8)) == 0 && keyCode >= 32 && keyCode <= 126)
            {
               const char character = static_cast<char>(std::tolower(static_cast<unsigned char>(keyCode)));
               if (std::isalnum(static_cast<unsigned char>(character)) || character == ' ' || character == '-' || character == '_')
               {
                  mSpawnMenuSearch.push_back(character);
                  mSpawnMenuSelectedIndex = 0;
               }
            }
            return;
         }

         if (keyCode == kKeySpace && mTransport)
         {
            mTransport->setPlaying(!mTransport->isPlaying());
            publishAudioGraphSnapshotLocked();
            return;
         }

         // Delete key removes selected module
         if (keyCode == kKeyDelete || keyCode == kKeyBackspace)
         {
            if (mDraggedModuleId >= 0)
            {
               deleteModule(mDraggedModuleId);
               mDraggedModuleId = -1;
            }
         }
      }

      void ModuleCanvas::openSpawnMenu(float x, float y)
      {
         Lock lock(mMutex);
         mSpawnMenuOpen = true;
         mSpawnMenuX = x;
         mSpawnMenuY = y;
         mSpawnMenuSearch.clear();
         mSpawnMenuSelectedIndex = 0;
      }

      void ModuleCanvas::closeSpawnMenu()
      {
         Lock lock(mMutex);
         mSpawnMenuOpen = false;
         mSpawnMenuCategory = -1;
      }

      bool ModuleCanvas::isTransportPlaying() const
      {
         Lock lock(mMutex);
         return mTransport && mTransport->isPlaying();
      }

      void ModuleCanvas::setTransportPlaying(bool playing)
      {
         Lock lock(mMutex);
         if (mTransport)
            mTransport->setPlaying(playing);
         publishAudioGraphSnapshotLocked();
      }

      void ModuleCanvas::toggleTransportPlaying()
      {
         Lock lock(mMutex);
         if (mTransport)
            mTransport->setPlaying(!mTransport->isPlaying());
         publishAudioGraphSnapshotLocked();
      }

      float ModuleCanvas::getTransportBPM() const
      {
         Lock lock(mMutex);
         return mTransport ? mTransport->getBPM() : 120.0f;
      }

      void ModuleCanvas::setTransportBPM(float bpm)
      {
         Lock lock(mMutex);
         if (mTransport)
            mTransport->setBPM(bpm);
         publishAudioGraphSnapshotLocked();
      }

   } // namespace wasm
} // namespace bespoke
