/**
 * BespokeSynth WASM - Modular Canvas Implementation
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "ModuleCanvas.h"
#include "BespokeWasm/Theme.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace bespoke {
   namespace wasm {

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

      void Module::renderTitleBar(IRenderer& renderer, float screenX, float screenY, float scale)
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

         // Module name (theme)
         renderer.fillColor(UITheme::kTextPrimary);
         renderer.fontSize(11.0f * scale);
         renderer.text(screenX + 5 * scale, screenY + 13 * scale, mName.c_str());

         // Enabled indicator
         if (!mEnabled)
         {
            renderer.fillColor(Color(0.8f, 0.2f, 0.2f, 0.7f));
            renderer.circle(screenX + w - 8 * scale, screenY + h * 0.5f, 3 * scale);
            renderer.fill();
         }
      }

      void Module::renderPorts(IRenderer& renderer, float screenX, float screenY, float scale)
      {
         float portSpacing = 15.0f * scale;

         // Input ports on the left
         for (size_t i = 0; i < mInputs.size(); i++)
         {
            float px = screenX;
            float py = screenY + (kTitleBarHeight + 10 + i * 15) * scale;

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

            renderer.fillColor(UITheme::kTextSecondary);
            renderer.fontSize(9.0f * scale);
            renderer.text(px + 8 * scale, py + 3 * scale, mInputs[i].name.c_str());
         }

         // Output ports on the right
         float moduleRight = screenX + mWidth * scale;
         for (size_t i = 0; i < mOutputs.size(); i++)
         {
            float px = moduleRight;
            float py = screenY + (kTitleBarHeight + 10 + i * 15) * scale;

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
         }
      }

      void Module::render(IRenderer& renderer, float offsetX, float offsetY, float scale)
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
         setSize(160, 110);
         addInput("note", PortType::Note);
         addInput("mod", PortType::Modulation);
         addOutput("audio", PortType::Audio);
      }

      void OscillatorModule::render(IRenderer& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         // Draw frequency display
         float contentY = screenY + kTitleBarHeight * scale + 25 * scale;
         renderer.fillColor(Color(0.7f, 0.7f, 0.75f, 1.0f));
         renderer.fontSize(10.0f * scale);

         char freqText[32];
         snprintf(freqText, sizeof(freqText), "%.1f Hz", mFrequency);
         renderer.fillColor(UITheme::kTextValue);
         renderer.text(screenX + 10 * scale, contentY, freqText);

         // Draw waveform indicator
         const char* waveNames[] = { "Sine", "Saw", "Square", "Tri" };
         renderer.fillColor(UITheme::kTextPrimary);
         renderer.text(screenX + 10 * scale, contentY + 15 * scale, waveNames[mWaveform % 4]);

         // Draw mini waveform preview
         float wfX = screenX + 10 * scale;
         float wfY = contentY + 25 * scale;
         float wfW = 60 * scale;
         float wfH = 25 * scale;

         renderer.strokeColor(Color(0.3f, 0.8f, 0.5f, 0.8f));
         renderer.strokeWidth(1.5f * scale);
         for (int i = 0; i < 20; i++)
         {
            float t1 = (float)i / 20.0f;
            float t2 = (float)(i + 1) / 20.0f;
            float y1 = sinf(t1 * 6.28f) * 0.5f;
            float y2 = sinf(t2 * 6.28f) * 0.5f;
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

      // ============================================================
      // GainModule
      // ============================================================

      GainModule::GainModule(int id)
         : Module(id, "gain", "Gain", ModuleCategory::AudioEffect)
      {
         setSize(120, 80);
         addInput("audio", PortType::Audio);
         addInput("mod", PortType::Modulation);
         addOutput("audio", PortType::Audio);
      }

      void GainModule::render(IRenderer& renderer, float offsetX, float offsetY, float scale)
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
         snprintf(gainText, sizeof(gainText), "%.1f dB", 20.0f * log10f(mGain + 0.001f));
         renderer.fillColor(Color(0.7f, 0.7f, 0.75f, 1.0f));
         renderer.fontSize(9.0f * scale);
         renderer.text(sliderX, sliderY + sliderH + 10 * scale, gainText);
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

      // ============================================================
      // OutputModule
      // ============================================================

      OutputModule::OutputModule(int id)
         : Module(id, "output", "Output", ModuleCategory::Other)
      {
         setSize(120, 90);
         addInput("audio", PortType::Audio);
      }

      void OutputModule::render(IRenderer& renderer, float offsetX, float offsetY, float scale)
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

         renderer.fillColor(Color(0.6f, 0.6f, 0.65f, 1.0f));
         renderer.fontSize(9.0f * scale);
         renderer.text(meterX, meterY + 50 * scale, "L    R");
      }

      // ============================================================
      // FilterModule
      // ============================================================

      FilterModule::FilterModule(int id)
         : Module(id, "filter", "Filter", ModuleCategory::AudioEffect)
      {
         setSize(150, 100);
         addInput("audio", PortType::Audio);
         addInput("cutoff", PortType::Modulation);
         addOutput("audio", PortType::Audio);
      }

      void FilterModule::render(IRenderer& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         float contentY = screenY + kTitleBarHeight * scale + 20 * scale;
         renderer.fillColor(Color(0.7f, 0.7f, 0.75f, 1.0f));
         renderer.fontSize(9.0f * scale);

         const char* typeNames[] = { "LPF", "HPF", "BPF" };
         renderer.text(screenX + 10 * scale, contentY, typeNames[mType % 3]);

         char cutText[32];
         snprintf(cutText, sizeof(cutText), "%.0f Hz", mCutoff);
         renderer.text(screenX + 10 * scale, contentY + 14 * scale, cutText);

         char resText[32];
         snprintf(resText, sizeof(resText), "Q: %.2f", mResonance);
         renderer.text(screenX + 10 * scale, contentY + 28 * scale, resText);

         // Simple filter response curve
         float curveX = screenX + 10 * scale;
         float curveY = contentY + 40 * scale;
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
         setSize(130, 90);
         addOutput("mod", PortType::Modulation);
      }

      void LFOModule::render(IRenderer& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;

         float contentY = screenY + kTitleBarHeight * scale + 15 * scale;
         renderer.fillColor(Color(0.7f, 0.7f, 0.75f, 1.0f));
         renderer.fontSize(9.0f * scale);

         char rateText[32];
         snprintf(rateText, sizeof(rateText), "Rate: %.2f Hz", mRate);
         renderer.text(screenX + 10 * scale, contentY, rateText);

         // Draw LFO waveform
         float wfX = screenX + 10 * scale;
         float wfY = contentY + 15 * scale;
         float wfW = 80 * scale;
         float wfH = 30 * scale;

         renderer.strokeColor(Color(0.7f, 0.7f, 0.2f, 0.9f));
         renderer.strokeWidth(1.5f * scale);
         for (int i = 0; i < 20; i++)
         {
            float t1 = (float)i / 20.0f;
            float t2 = (float)(i + 1) / 20.0f;
            float y1 = sinf(t1 * 6.28f * 2.0f) * 0.5f * mDepth;
            float y2 = sinf(t2 * 6.28f * 2.0f) * 0.5f * mDepth;
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

      void TransportModule::render(IRenderer& renderer, float offsetX, float offsetY, float scale)
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
         renderer.fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
         renderer.fontSize(14.0f * scale);
         renderer.text(btnX + 25 * scale, screenY + 26 * scale, bpmText);

         // Time signature
         char timeSigText[16];
         snprintf(timeSigText, sizeof(timeSigText), "%d/%d", mTimeSigTop, mTimeSigBottom);
         renderer.fillColor(Color(0.7f, 0.7f, 0.75f, 1.0f));
         renderer.fontSize(11.0f * scale);
         renderer.text(screenX + 140 * scale, screenY + 26 * scale, timeSigText);

         // Swing
         char swingText[16];
         snprintf(swingText, sizeof(swingText), "Sw:%.0f%%", mSwing * 100.0f);
         renderer.text(screenX + 185 * scale, screenY + 26 * scale, swingText);

         // Label
         renderer.fillColor(Color(0.5f, 0.5f, 0.55f, 1.0f));
         renderer.fontSize(9.0f * scale);
         renderer.text(screenX + 10 * scale, screenY + 50 * scale, "Transport");
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

      void ScaleModule::render(IRenderer& renderer, float offsetX, float offsetY, float scale)
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

         // Root note
         renderer.fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
         renderer.fontSize(13.0f * scale);
         renderer.text(screenX + 10 * scale, screenY + 22 * scale, kNoteNames[mRootNote % 12]);

         // Scale type
         renderer.fillColor(Color(0.7f, 0.7f, 0.75f, 1.0f));
         renderer.fontSize(11.0f * scale);
         renderer.text(screenX + 35 * scale, screenY + 22 * scale, kScaleNames[mScaleType % 7]);

         // Piano keys visualization (mini)
         float keyX = screenX + 10 * scale;
         float keyY = screenY + 35 * scale;
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

         // Label
         renderer.fillColor(Color(0.5f, 0.5f, 0.55f, 1.0f));
         renderer.fontSize(9.0f * scale);
         renderer.text(screenX + 140 * scale, screenY + 60 * scale, "Scale");
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
         mTypes.push_back({"oscillator", "Oscillator", ModuleCategory::Synth});
         mTypes.push_back({"gain", "Gain", ModuleCategory::AudioEffect});
         mTypes.push_back({"output", "Output", ModuleCategory::Other});
         mTypes.push_back({"filter", "Filter", ModuleCategory::AudioEffect});
         mTypes.push_back({"lfo", "LFO", ModuleCategory::Modulator});
         mTypes.push_back({"transport", "Transport", ModuleCategory::Other});
         mTypes.push_back({"scale", "Scale", ModuleCategory::Other});
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
            if (t.category == category)
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

         printf("ModuleCanvas: Created with Transport and Scale modules\n");
      }

      int ModuleCanvas::createModule(const std::string& type, float x, float y)
      {
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
         return id;
      }

      void ModuleCanvas::deleteModule(int moduleId)
      {
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
      }

      Module* ModuleCanvas::getModule(int moduleId)
      {
         auto it = mModules.find(moduleId);
         if (it != mModules.end())
            return it->second.get();
         return nullptr;
      }

      void ModuleCanvas::connectModules(int sourceId, int sourcePort, int destId, int destPort)
      {
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
      }

      void ModuleCanvas::disconnectModules(int sourceId, int destId)
      {
         mConnections.erase(
            std::remove_if(mConnections.begin(), mConnections.end(),
                           [sourceId, destId](const Connection& c)
                           {
                              return c.sourceModuleId == sourceId && c.destModuleId == destId;
                           }),
            mConnections.end());
      }

      void ModuleCanvas::zoom(float factor, float centerX, float centerY)
      {
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

      void ModuleCanvas::renderTitleBar(IRenderer& renderer, int viewWidth)
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
         renderer.fillColor(Color(0.9f, 0.9f, 0.95f, 1.0f));
         renderer.fontSize(14.0f);
         renderer.text(10, 26, "BespokeSynth");

         // Spawn menu buttons by category
         const char* catNames[] = {"Synth", "Audio FX", "Modulators", "Other"};
         ModuleCategory catValues[] = {
            ModuleCategory::Synth,
            ModuleCategory::AudioEffect,
            ModuleCategory::Modulator,
            ModuleCategory::Other
         };

         float btnX = 140.0f;
         float btnW = 75.0f;
         float btnH = 24.0f;
         float btnY = 8.0f;

         for (int i = 0; i < 4; i++)
         {
            bool isActive = mSpawnMenuOpen && mSpawnMenuCategory == i;

            if (isActive)
            {
               renderer.fillColor(Color(0.25f, 0.25f, 0.3f, 1.0f));
            }
            else
            {
               renderer.fillColor(Color(0.18f, 0.18f, 0.2f, 1.0f));
            }
            renderer.roundedRect(btnX + i * (btnW + 5), btnY, btnW, btnH, 3.0f);
            renderer.fill();

            renderer.strokeColor(getCategoryColor(catValues[i]));
            renderer.strokeWidth(1.0f);
            renderer.roundedRect(btnX + i * (btnW + 5), btnY, btnW, btnH, 3.0f);
            renderer.stroke();

            renderer.fillColor(Color(0.8f, 0.8f, 0.85f, 1.0f));
            renderer.fontSize(11.0f);
            renderer.text(btnX + i * (btnW + 5) + 8, btnY + 16, catNames[i]);
         }

         // Render spawn dropdown if open
         if (mSpawnMenuOpen && mSpawnMenuCategory >= 0 && mSpawnMenuCategory < 4)
         {
            ModuleCategory cat = catValues[mSpawnMenuCategory];
            auto types = ModuleFactory::instance().getTypesByCategory(cat);

            float dropX = 140.0f + mSpawnMenuCategory * (btnW + 5);
            float dropY = kTitleBarHeight;
            float dropW = 140.0f;
            float dropH = types.size() * 25.0f + 10.0f;

            // Dropdown background
            renderer.fillColor(Color(0.15f, 0.15f, 0.17f, 0.98f));
            renderer.roundedRect(dropX, dropY, dropW, dropH, 4.0f);
            renderer.fill();

            renderer.strokeColor(Color(0.35f, 0.35f, 0.4f, 1.0f));
            renderer.strokeWidth(1.0f);
            renderer.roundedRect(dropX, dropY, dropW, dropH, 4.0f);
            renderer.stroke();

            // Module type entries
            for (size_t i = 0; i < types.size(); i++)
            {
               float entryY = dropY + 5 + i * 25.0f;

               renderer.fillColor(Color(0.85f, 0.85f, 0.9f, 1.0f));
               renderer.fontSize(11.0f);
               renderer.text(dropX + 10, entryY + 16, types[i].displayName.c_str());
            }
         }
      }

      void ModuleCanvas::render(IRenderer& renderer, int viewWidth, int viewHeight)
      {
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
                          conn.sourcePortIndex * 15 + mOffsetY) * mScale + canvasTop;
            float dstX = (dstMod->getX() + mOffsetX) * mScale;
            float dstY = (dstMod->getY() + Module::kTitleBarHeight + 10 +
                          conn.destPortIndex * 15 + mOffsetY) * mScale + canvasTop;

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
                             mConnectSourcePort * 15 + mOffsetY) * mScale + canvasTop;
               renderer.drawCableWithSag(srcX, srcY, mConnectEndX, mConnectEndY,
                                         Color(0.7f, 0.7f, 0.75f, 0.6f), 2.0f, 0.15f);
            }
         }

         // Draw modules
         for (auto& [id, module] : mModules)
         {
            module->render(renderer, mOffsetX, mOffsetY + canvasTop / mScale, mScale);
         }

         // Render title bar on top
         renderTitleBar(renderer, viewWidth);

         // Draw zoom indicator
         renderer.fillColor(Color(0.5f, 0.5f, 0.55f, 0.7f));
         renderer.fontSize(10.0f);
         char zoomText[32];
         snprintf(zoomText, sizeof(zoomText), "%.0f%%", mScale * 100.0f);
         renderer.text(static_cast<float>(viewWidth) - 50, static_cast<float>(viewHeight) - 15, zoomText);

         // Module count
         char countText[64];
         snprintf(countText, sizeof(countText), "Modules: %d | Connections: %d",
                  static_cast<int>(mModules.size()), static_cast<int>(mConnections.size()));
         renderer.text(10, static_cast<float>(viewHeight) - 15, countText);
      }

      void ModuleCanvas::onMouseDown(float x, float y, int button)
      {
         // Check title bar spawn menu buttons
         if (y < kTitleBarHeight)
         {
            float btnX = 140.0f;
            float btnW = 75.0f;
            float btnH = 24.0f;
            float btnTop = 8.0f;

            for (int i = 0; i < 4; i++)
            {
               float bx = btnX + i * (btnW + 5);
               if (x >= bx && x <= bx + btnW && y >= btnTop && y <= btnTop + btnH)
               {
                  if (mSpawnMenuOpen && mSpawnMenuCategory == i)
                  {
                     closeSpawnMenu();
                  }
                  else
                  {
                     openSpawnMenu(x, y);
                     mSpawnMenuCategory = i;
                  }
                  return;
               }
            }

            // Close menu if clicking elsewhere on title bar
            if (mSpawnMenuOpen)
            {
               closeSpawnMenu();
            }
            return;
         }

         // Check spawn menu dropdown clicks
         if (mSpawnMenuOpen)
         {
            ModuleCategory catValues[] = {
               ModuleCategory::Synth,
               ModuleCategory::AudioEffect,
               ModuleCategory::Modulator,
               ModuleCategory::Other
            };

            if (mSpawnMenuCategory >= 0 && mSpawnMenuCategory < 4)
            {
               ModuleCategory cat = catValues[mSpawnMenuCategory];
               auto types = ModuleFactory::instance().getTypesByCategory(cat);

               float btnW = 75.0f;
               float dropX = 140.0f + mSpawnMenuCategory * (btnW + 5);
               float dropY = kTitleBarHeight;
               float dropW = 140.0f;

               for (size_t i = 0; i < types.size(); i++)
               {
                  float entryY = dropY + 5 + i * 25.0f;
                  if (x >= dropX && x <= dropX + dropW && y >= entryY && y <= entryY + 25.0f)
                  {
                     // Spawn module near the spawn menu location
                     float worldX = screenToWorldX(mSpawnMenuX + 100.0f);
                     float worldY = screenToWorldY(mSpawnMenuY + 60.0f);
                     createModule(types[i].type, worldX, worldY);
                     closeSpawnMenu();
                     return;
                  }
               }
            }

            closeSpawnMenu();
            return;
         }

         // Convert to world coordinates for module hit testing
         float worldX = screenToWorldX(x);
         float worldY = screenToWorldY(y - kTitleBarHeight);

         // Check module hit testing
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
         mDraggedModuleId = -1;
         mIsPanning = false;
         mIsConnecting = false;
      }

      void ModuleCanvas::onMouseMove(float x, float y, float prevX, float prevY)
      {
         if (mDraggedModuleId >= 0)
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
         }
      }

      void ModuleCanvas::onMouseWheel(float deltaX, float deltaY, float mouseX, float mouseY)
      {
         float zoomFactor = 1.0f - deltaY * 0.001f;
         zoom(zoomFactor, mouseX, mouseY);
      }

      void ModuleCanvas::onKeyDown(int keyCode, int modifiers)
      {
         static const int kKeyDelete = 46;
         static const int kKeyBackspace = 8;

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
         mSpawnMenuOpen = true;
         mSpawnMenuX = x;
         mSpawnMenuY = y;
      }

      void ModuleCanvas::closeSpawnMenu()
      {
         mSpawnMenuOpen = false;
         mSpawnMenuCategory = -1;
      }

   } // namespace wasm
} // namespace bespoke
