/**
 * BespokeSynth WASM - Concrete canvas module declarations
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/Module.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/TuningTable.h"

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
         std::string controlNameAt(float worldX, float worldY) const override;

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
         std::string controlNameAt(float worldX, float worldY) const override;

      private:
         float mGain = 0.7f;
         bool mDraggingSlider = false;
      };

      class OutputModule : public Module
      {
      public:
         OutputModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         /** `limit` is serialized; `meterL`/`meterR` are live pre-limit peaks fed by the canvas each frame. */
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         bool handleMouseDown(float worldX, float worldY) override;
         std::string controlNameAt(float worldX, float worldY) const override;

      private:
         bool hitLimitToggle(float worldX, float worldY) const;

         float mLevel = 0.0f;
         float mMeter[2] = { 0.0f, 0.0f };
         int mClipHoldFrames = 0;
         bool mLimit = true;
      };

      class FilterModule : public Module
      {
      public:
         FilterModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;

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
         std::string controlNameAt(float worldX, float worldY) const override;

      private:
         float mRate = 1.0f;
         float mDepth = 1.0f;
         int mShape = 0;
      };

      class AdsrModule : public Module
      {
      public:
         AdsrModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;

      private:
         float mAttack = 10.0f;
         float mDecay = 120.0f;
         float mSustain = 0.7f;
         float mRelease = 200.0f;
      };

      class DelayModule : public Module
      {
      public:
         DelayModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;

      private:
         float mTime = 0.25f;
         float mFeedback = 0.35f;
         float mMix = 0.35f;
         float mPingpong = 0.0f;
      };

      class NoiseModule : public Module
      {
      public:
         NoiseModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;

      private:
         float mVolume = 0.35f;
         int mColor = 0;
      };

      class StepSequencerModule : public Module
      {
      public:
         static constexpr int kStepCount = 16;

         StepSequencerModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;
         bool handleMouseDown(float worldX, float worldY) override;

         bool isStepActive(int step) const;
         void setStepActive(int step, bool active);

      private:
         int mPatternMask = 0x1111;
         int mPitch = 60;
         float mGate = 0.75f;
         int mSteps = kStepCount;
      };

      class TransportModule : public Module
      {
      public:
         TransportModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;

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

      class SamplerModule : public Module
      {
      public:
         SamplerModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;
         void setStringProperty(const std::string& name, const std::string& value) override;
         std::string getStringProperty(const std::string& name) const override;
         std::vector<std::string> stringPropertyNames() const override;
         bool handleMouseDown(float worldX, float worldY) override;

      private:
         void refreshSample();

         std::string mSampleHash;
         std::string mSampleName;
         /** Resolved per frame: the store may evict and reload the buffer behind it. */
         SampleHandle mSampleHandle;
         float mVolume = 0.85f;
         float mStart = 0.0f;
         float mEnd = 1.0f;
         float mLoopStart = 0.0f;
         float mLoopEnd = 1.0f;
         int mMode = 0;
         int mRootPitch = 60;
      };

      class LooperModule : public Module
      {
      public:
         LooperModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;
         bool handleMouseDown(float worldX, float worldY) override;

      private:
         float mMix = 1.0f;
         int mBars = 1;
         int mCommand = 0;
      };

      class PulseToNoteModule : public Module
      {
      public:
         PulseToNoteModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;

      private:
         int mPitch = 60;
         float mGate = 0.75f;
      };

      class ScriptModule : public Module
      {
      public:
         ScriptModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setStringProperty(const std::string& name, const std::string& value) override;
         std::string getStringProperty(const std::string& name) const override;
         std::vector<std::string> stringPropertyNames() const override;

      private:
         std::string mScript;
         std::string mStatus;
      };

      class ScaleModule : public Module
      {
      public:
         ScaleModule(int id);
         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         std::string controlNameAt(float worldX, float worldY) const override;
         /** "scl"/"kbm" hold Scala text; "sclName"/"kbmName" the imported file names. */
         void setStringProperty(const std::string& name, const std::string& value) override;
         std::string getStringProperty(const std::string& name) const override;
         std::vector<std::string> stringPropertyNames() const override;

         bool hasCustomTuning() const { return mCustomTuning; }
         const NoteFrequencyTable& tuningTable() const { return mTuningTable; }
         const std::string& tuningError() const { return mTuningError; }

      private:
         void rebuildTuning();

         int mRootNote = 0;
         int mScaleType = 0;
         std::string mScl;
         std::string mSclName;
         std::string mKbm;
         std::string mKbmName;
         std::string mTuningDescription;
         std::string mTuningError;
         bool mCustomTuning = false;
         int mTuningDegrees = 12;
         NoteFrequencyTable mTuningTable{};
         static const char* const kNoteNames[];
         static const char* const kScaleNames[];
      };

   } // namespace wasm
} // namespace bespoke
