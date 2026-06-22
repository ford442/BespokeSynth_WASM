/**
 * BespokeSynth WASM - Modular Canvas System
 * Provides module registry, placement, connection graph, and canvas rendering
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "Renderer2D.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

namespace bespoke {
   namespace wasm {

      // Module categories matching desktop BespokeSynth
      enum class ModuleCategory
      {
         Instrument,
         NoteEffect,
         Synth,
         AudioEffect,
         Modulator,
         Pulse,
         Other
      };

      // Port types for module connections
      enum class PortType
      {
         Audio,
         Note,
         Pulse,
         Modulation
      };

      // A port on a module (input or output)
      struct Port
      {
         std::string name;
         PortType type;
         bool isOutput;
         float x, y; // Relative position on module

         Port(const std::string& name, PortType type, bool isOutput)
            : name(name), type(type), isOutput(isOutput), x(0), y(0)
         {
         }
      };

      // A connection between two modules
      struct Connection
      {
         int sourceModuleId;
         int sourcePortIndex;
         int destModuleId;
         int destPortIndex;
         Color color;

         Connection()
            : sourceModuleId(-1), sourcePortIndex(0), destModuleId(-1), destPortIndex(0)
         {
         }
      };

      // Base module class - all modules inherit from this
      class Module
      {
      public:
         Module(int id, const std::string& type, const std::string& name, ModuleCategory category);
         virtual ~Module() = default;

         // Identification
         int getId() const { return mId; }
         const std::string& getType() const { return mType; }
         const std::string& getName() const { return mName; }
         ModuleCategory getCategory() const { return mCategory; }

         // Position and size
         float getX() const { return mX; }
         float getY() const { return mY; }
         float getWidth() const { return mWidth; }
         float getHeight() const { return mHeight; }
         void setPosition(float x, float y) { mX = x; mY = y; }
         void setSize(float w, float h) { mWidth = w; mHeight = h; }

         // State
         bool isEnabled() const { return mEnabled; }
         void setEnabled(bool enabled) { mEnabled = enabled; }
         bool isMinimized() const { return mMinimized; }
         void setMinimized(bool minimized) { mMinimized = minimized; }

         // Ports
         const std::vector<Port>& getInputs() const { return mInputs; }
         const std::vector<Port>& getOutputs() const { return mOutputs; }

         // Rendering
         virtual void render(Renderer2D& renderer, float canvasOffsetX, float canvasOffsetY, float scale);

         // Hit testing
         bool hitTest(float worldX, float worldY) const;
         bool hitTitleBar(float worldX, float worldY) const;

         // Controls
         virtual void setControlValue(const std::string& name, float value) {}
         virtual float getControlValue(const std::string& name) const { return 0.0f; }

         // Optional in-module control interaction (slider drags, etc.)
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

      // Specific module implementations

      class OscillatorModule : public Module
      {
      public:
         OscillatorModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         bool handleMouseDown(float worldX, float worldY) override;
         bool handleMouseDrag(float worldX, float worldY, float dx, float dy) override;
         void handleMouseUp() override;

      private:
         float mFrequency = 440.0f;
         float mVolume = 0.7f;
         int mWaveform = 0; // 0=sine, 1=saw, 2=square, 3=triangle
         bool mDraggingFreq = false;
      };

      class GainModule : public Module
      {
      public:
         GainModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         bool handleMouseDown(float worldX, float worldY) override;
         bool handleMouseDrag(float worldX, float worldY, float dx, float dy) override;
         void handleMouseUp() override;

      private:
         float mGain = 0.7f;
         bool mDraggingSlider = false;
      };

      class OutputModule : public Module
      {
      public:
         OutputModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;

      private:
         float mLevel = 0.0f;
      };

      class FilterModule : public Module
      {
      public:
         FilterModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;

      private:
         float mCutoff = 1000.0f;
         float mResonance = 0.5f;
         int mType = 0; // 0=lowpass, 1=highpass, 2=bandpass
      };

      class LFOModule : public Module
      {
      public:
         LFOModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;

      private:
         float mRate = 1.0f;
         float mDepth = 1.0f;
         int mShape = 0;
      };

      // Transport singleton module
      class TransportModule : public Module
      {
      public:
         TransportModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;

         bool isPlaying() const { return mPlaying; }
         void setPlaying(bool playing) { mPlaying = playing; }
         float getBPM() const { return mBPM; }
         void setBPM(float bpm) { mBPM = bpm; }

      private:
         float mBPM = 120.0f;
         int mTimeSigTop = 4;
         int mTimeSigBottom = 4;
         float mSwing = 0.0f;
         bool mPlaying = false;
      };

      // Scale singleton module
      class ScaleModule : public Module
      {
      public:
         ScaleModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;

      private:
         int mRootNote = 0; // C
         int mScaleType = 0; // 0=major, 1=minor, etc.
         static const char* const kNoteNames[];
         static const char* const kScaleNames[];
      };

      // Module factory - creates modules by type name
      struct ModuleTypeInfo
      {
         std::string type;
         std::string displayName;
         ModuleCategory category;
      };

      class ModuleFactory
      {
      public:
         static ModuleFactory& instance();

         std::unique_ptr<Module> createModule(const std::string& type, int id);
         const std::vector<ModuleTypeInfo>& getRegisteredTypes() const { return mTypes; }
         std::vector<ModuleTypeInfo> getTypesByCategory(ModuleCategory category) const;

      private:
         ModuleFactory();
         std::vector<ModuleTypeInfo> mTypes;
      };

      // The main modular canvas - manages modules and connections
      class ModuleCanvas
      {
      public:
         ModuleCanvas();
         ~ModuleCanvas() = default;

         // Module management
         int createModule(const std::string& type, float x, float y);
         void deleteModule(int moduleId);
         Module* getModule(int moduleId);
         int findFirstModuleOfType(const std::string& type) const;
         int getModuleCount() const { return static_cast<int>(mModules.size()); }

         // Connection management
         void connectModules(int sourceId, int sourcePort, int destId, int destPort);
         void disconnectModules(int sourceId, int destId);
         const std::vector<Connection>& getConnections() const { return mConnections; }

         // Canvas view
         float getOffsetX() const { return mOffsetX; }
         float getOffsetY() const { return mOffsetY; }
         float getScale() const { return mScale; }
         void setOffset(float x, float y) { mOffsetX = x; mOffsetY = y; }
         void pan(float dx, float dy) { mOffsetX += dx; mOffsetY += dy; }
         void zoom(float factor, float centerX, float centerY);

         // Rendering
         void render(Renderer2D& renderer, int viewWidth, int viewHeight);
         void renderTitleBar(Renderer2D& renderer, int viewWidth);
         void renderTransport(Renderer2D& renderer, int viewWidth);

         // Input handling
         void onMouseDown(float x, float y, int button);
         void onMouseUp(float x, float y, int button);
         void onMouseMove(float x, float y, float prevX, float prevY);
         void onMouseWheel(float deltaX, float deltaY, float mouseX, float mouseY);
         void onKeyDown(int keyCode, int modifiers);

         // Sync output meter from audio engine (call each frame before render)
         void setOutputLevel(float level);

         // Render-test / regression helpers
         void clearUserModules();
         void setViewTransform(float offsetX, float offsetY, float scale);
         void setupCanonicalRenderTestScene();

         // Spawn menu
         bool isSpawnMenuOpen() const { return mSpawnMenuOpen; }
         void openSpawnMenu(float x, float y);
         void closeSpawnMenu();
         int getSpawnMenuCategory() const { return mSpawnMenuCategory; }
         void setSpawnMenuCategory(int cat) { mSpawnMenuCategory = cat; }

         // Transport access
         TransportModule* getTransport() { return mTransport; }
         ScaleModule* getScale() { return mScaleModule; }

      private:
         // Convert screen coordinates to world coordinates
         float screenToWorldX(float screenX) const;
         float screenToWorldY(float screenY) const;

         // Module storage
         std::unordered_map<int, std::unique_ptr<Module>> mModules;
         std::vector<Connection> mConnections;
         int mNextModuleId = 1;

         // Canvas view state
         float mOffsetX = 0.0f;
         float mOffsetY = 0.0f;
         float mScale = 1.0f;

         // Interaction state
         int mDraggedModuleId = -1;
         int mControlModuleId = -1;
         bool mIsPanning = false;
         float mPanStartX = 0.0f, mPanStartY = 0.0f;
         bool mIsConnecting = false;
         int mConnectSourceId = -1;
         int mConnectSourcePort = -1;
         float mConnectEndX = 0.0f, mConnectEndY = 0.0f;

         // Spawn menu state
         bool mSpawnMenuOpen = false;
         float mSpawnMenuX = 0.0f, mSpawnMenuY = 0.0f;
         int mSpawnMenuCategory = -1;

         // Singleton modules (always present)
         TransportModule* mTransport = nullptr;
         ScaleModule* mScaleModule = nullptr;

         // Title bar constants
         static constexpr float kTitleBarHeight = 40.0f;
         static constexpr float kTransportBarHeight = 30.0f;

         // Category colors
         static Color getCategoryColor(ModuleCategory cat);
      };

   } // namespace wasm
} // namespace bespoke
