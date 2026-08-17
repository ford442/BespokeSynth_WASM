/**
 * BespokeSynth WASM - Base module class
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/ModuleTypes.h"
#include "BespokeWasm/Renderer2D.h"
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class Module
      {
      public:
         Module(int id, const std::string& type, const std::string& name, ModuleCategory category);
         virtual ~Module() = default;

         int getId() const { return mId; }
         const std::string& getType() const { return mType; }
         const std::string& getName() const { return mName; }
         ModuleCategory getCategory() const { return mCategory; }

         float getX() const { return mX; }
         float getY() const { return mY; }
         float getWidth() const { return mWidth; }
         float getHeight() const { return mHeight; }
         void setPosition(float x, float y)
         {
            mX = x;
            mY = y;
         }
         void setSize(float w, float h)
         {
            mWidth = w;
            mHeight = h;
         }

         bool isEnabled() const { return mEnabled; }
         void setEnabled(bool enabled) { mEnabled = enabled; }
         bool isMinimized() const { return mMinimized; }
         void setMinimized(bool minimized) { mMinimized = minimized; }

         const std::vector<Port>& getInputs() const { return mInputs; }
         const std::vector<Port>& getOutputs() const { return mOutputs; }

         virtual void render(Renderer2D& renderer, float canvasOffsetX, float canvasOffsetY, float scale);

         bool hitTest(float worldX, float worldY) const;
         bool hitTitleBar(float worldX, float worldY) const;

         virtual void setControlValue(const std::string& name, float value) {}
         virtual float getControlValue(const std::string& name) const { return 0.0f; }

         virtual void setStringProperty(const std::string& name, const std::string& value)
         {
            (void)name;
            (void)value;
         }
         virtual std::string getStringProperty(const std::string& name) const
         {
            (void)name;
            return {};
         }
         virtual std::vector<std::string> stringPropertyNames() const { return {}; }

         virtual bool handleMouseDown(float worldX, float worldY) { return false; }
         virtual bool handleMouseDrag(float worldX, float worldY, float dx, float dy) { return false; }
         virtual void handleMouseUp() {}

      protected:
         void addInput(const std::string& name, PortType type);
         void addOutput(const std::string& name, PortType type);
         void renderTitleBar(Renderer2D& renderer, float screenX, float screenY, float scale);
         void renderPorts(Renderer2D& renderer, float screenX, float screenY, float scale);

         int mId;
         std::string mType;
         std::string mName;
         ModuleCategory mCategory;
         float mX, mY;
         float mWidth, mHeight;
         bool mEnabled;
         bool mMinimized;
         std::vector<Port> mInputs;
         std::vector<Port> mOutputs;

      public:
         static constexpr float kTitleBarHeight = 18.0f;
         static constexpr float kPortRadius = 5.0f;
      };

   } // namespace wasm
} // namespace bespoke
