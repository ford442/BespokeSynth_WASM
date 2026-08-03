/**
 * BespokeSynth WASM - Concrete canvas module declarations
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/Module.h"

namespace bespoke
{
   namespace wasm
   {

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
         int mWaveform = 0;
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
         int mType = 0;
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

      class ScaleModule : public Module
      {
      public:
         ScaleModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;

      private:
         int mRootNote = 0;
         int mScaleType = 0;
         static const char* const kNoteNames[];
         static const char* const kScaleNames[];
      };

   } // namespace wasm
} // namespace bespoke
