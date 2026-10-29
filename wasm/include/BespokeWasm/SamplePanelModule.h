/**
 * BespokeSynth WASM - Slider panel with a sample (granular synth canvas UI)
 *
 * A SliderPanelModule that also holds the `sampleHash` string property, so a dropped
 * or assigned sample saves with the patch as extras["sampleHash"] (the adapter binds
 * it through SampleSource.h), and draws the sample's waveform above the sliders with
 * a marker at the `markerControl` position (0..1).
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/SliderPanelModule.h"

namespace bespoke
{
   namespace wasm
   {

      class SamplePanelModule : public SliderPanelModule
      {
      public:
         static constexpr float kWaveHeight = 40.0f;

         SamplePanelModule(int id,
                           const std::string& type,
                           const std::string& name,
                           ModuleCategory category,
                           std::vector<SliderSpec> specs,
                           float width,
                           const char* markerControl,
                           std::string defaultSampleHash = {});

         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setStringProperty(const std::string& name, const std::string& value) override;
         std::string getStringProperty(const std::string& name) const override;
         std::vector<std::string> stringPropertyNames() const override;

      private:
         void refreshSample();

         std::string mSampleHash;
         std::string mSampleName;
         SampleHandle mSampleHandle;
         const char* mMarkerControl;
      };

   } // namespace wasm
} // namespace bespoke
