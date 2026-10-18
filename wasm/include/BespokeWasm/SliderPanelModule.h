/**
 * BespokeSynth WASM - Generic slider-panel canvas UI
 *
 * Shared UI for adapters whose controls are a flat list of ranged values
 * (mixer, compressor, chorus, waveshaper, clock divider). Each row is a
 * draggable bar labelled with the control name and formatted value. Rows sit
 * below the port column so labels never overlap cables.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/Module.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      enum class SliderFormat
      {
         Percent, // 0..1 shown as %
         Decimal, // two decimals
         Integer, // snapped to whole numbers
         Decibels, // value already in dB
         Hertz,
         Millis,
         Ratio, // "4.0:1"
         Choice, // snapped index into choices
         Pan, // 0..1 shown as L100 / C / R100
      };

      struct SliderSpec
      {
         const char* name = "";
         const char* label = "";
         float minValue = 0.0f;
         float maxValue = 1.0f;
         float defaultValue = 0.0f;
         SliderFormat format = SliderFormat::Percent;
         std::vector<const char*> choices;
      };

      /** Control descriptors (serialized names + defaults) straight from the slider table. */
      std::vector<WasmControlDescriptor> controlDescriptorsFromSpecs(const std::vector<SliderSpec>& specs);

      /** Control value for `name`, clamped to its spec range; default when absent. */
      float sliderSpecValue(const std::vector<SliderSpec>& specs, const WasmControlMap& controls, const char* name);

      class SliderPanelModule : public Module
      {
      public:
         SliderPanelModule(int id,
                           const std::string& type,
                           const std::string& name,
                           ModuleCategory category,
                           std::vector<SliderSpec> specs,
                           float width = 170.0f);

         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         bool handleMouseDown(float worldX, float worldY) override;
         bool handleMouseDrag(float worldX, float worldY, float dx, float dy) override;
         void handleMouseUp() override;
         std::string controlNameAt(float worldX, float worldY) const override;
         bool controlRange(const std::string& name, float& minValue, float& maxValue) const override;

         static constexpr float kRowHeight = 14.0f;
         static constexpr float kRowStep = 18.0f;
         static constexpr float kSidePad = 10.0f;

      protected:
         float slidersTop() const;
         int rowAt(float worldX, float worldY) const;
         void setFromWorldX(int row, float worldX);
         float snap(int row, float value) const;
         void formatValue(int row, char* buf, size_t len) const;

         std::vector<SliderSpec> mSpecs;
         std::vector<float> mValues;
         int mDragRow = -1;
      };

   } // namespace wasm
} // namespace bespoke
