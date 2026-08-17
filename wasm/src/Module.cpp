/**
 * BespokeSynth WASM
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/Module.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/WasmModuleAdapter.h"

namespace bespoke
{
   namespace wasm
   {
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
         if (const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find(type))
         {
            for (const auto& port : adapter->inputPorts())
               addInput(port.name, port.type);
            for (const auto& port : adapter->outputPorts())
               addOutput(port.name, port.type);
         }
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

   }
}
