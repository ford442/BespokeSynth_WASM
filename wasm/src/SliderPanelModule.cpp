/**
 * BespokeSynth WASM - Generic slider-panel canvas UI
 */

#include "BespokeWasm/SliderPanelModule.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr float kPortStep = 15.0f;
      }

      std::vector<WasmControlDescriptor> controlDescriptorsFromSpecs(const std::vector<SliderSpec>& specs)
      {
         std::vector<WasmControlDescriptor> descriptors;
         descriptors.reserve(specs.size());
         for (const auto& spec : specs)
            descriptors.push_back({ spec.name, spec.defaultValue });
         return descriptors;
      }

      float sliderSpecValue(const std::vector<SliderSpec>& specs, const WasmControlMap& controls, const char* name)
      {
         for (const auto& spec : specs)
         {
            if (std::string(spec.name) != name)
               continue;
            const float value = wasmControlValue(controls, name, spec.defaultValue);
            return std::max(spec.minValue, std::min(spec.maxValue, value));
         }
         return wasmControlValue(controls, name, 0.0f);
      }

      SliderPanelModule::SliderPanelModule(int id,
                                           const std::string& type,
                                           const std::string& name,
                                           ModuleCategory category,
                                           std::vector<SliderSpec> specs,
                                           float width)
      : Module(id, type, name, category)
      , mSpecs(std::move(specs))
      {
         mValues.reserve(mSpecs.size());
         for (const auto& spec : mSpecs)
            mValues.push_back(spec.defaultValue);
         setSize(width, slidersTop() + static_cast<float>(mSpecs.size()) * kRowStep + 4.0f);
      }

      float SliderPanelModule::slidersTop() const
      {
         const size_t portRows = std::max(mInputs.size(), mOutputs.size());
         return kTitleBarHeight + 6.0f + static_cast<float>(portRows) * kPortStep;
      }

      int SliderPanelModule::rowAt(float worldX, float worldY) const
      {
         const float localX = worldX - mX;
         const float localY = worldY - mY - slidersTop();
         if (localX < kSidePad || localX > mWidth - kSidePad || localY < 0.0f)
            return -1;
         const int row = static_cast<int>(localY / kRowStep);
         if (row < 0 || row >= static_cast<int>(mSpecs.size()))
            return -1;
         if (localY - static_cast<float>(row) * kRowStep > kRowHeight)
            return -1;
         return row;
      }

      std::string SliderPanelModule::controlNameAt(float worldX, float worldY) const
      {
         const int row = rowAt(worldX, worldY);
         return row >= 0 ? std::string(mSpecs[static_cast<size_t>(row)].name) : std::string();
      }

      bool SliderPanelModule::controlRange(const std::string& name, float& minValue, float& maxValue) const
      {
         for (const auto& spec : mSpecs)
         {
            if (name != spec.name)
               continue;
            minValue = spec.minValue;
            maxValue = spec.maxValue;
            return true;
         }
         return false;
      }

      float SliderPanelModule::snap(int row, float value) const
      {
         const SliderSpec& spec = mSpecs[static_cast<size_t>(row)];
         value = std::max(spec.minValue, std::min(spec.maxValue, value));
         if (spec.format == SliderFormat::Integer || spec.format == SliderFormat::Choice)
            value = std::round(value);
         return value;
      }

      void SliderPanelModule::setFromWorldX(int row, float worldX)
      {
         const SliderSpec& spec = mSpecs[static_cast<size_t>(row)];
         const float trackW = std::max(1.0f, mWidth - 2.0f * kSidePad);
         const float t = std::max(0.0f, std::min(1.0f, (worldX - mX - kSidePad) / trackW));
         mValues[static_cast<size_t>(row)] = snap(row, spec.minValue + t * (spec.maxValue - spec.minValue));
      }

      void SliderPanelModule::formatValue(int row, char* buf, size_t len) const
      {
         const SliderSpec& spec = mSpecs[static_cast<size_t>(row)];
         const float value = mValues[static_cast<size_t>(row)];
         switch (spec.format)
         {
            case SliderFormat::Percent:
               snprintf(buf, len, "%.0f%%", value * 100.0f);
               break;
            case SliderFormat::Decimal:
               snprintf(buf, len, "%.2f", value);
               break;
            case SliderFormat::Integer:
               snprintf(buf, len, "%d", static_cast<int>(std::lround(value)));
               break;
            case SliderFormat::Decibels:
               snprintf(buf, len, "%.1f dB", value);
               break;
            case SliderFormat::Hertz:
               formatFrequency(value, buf, len);
               break;
            case SliderFormat::Millis:
               snprintf(buf, len, "%.0f ms", value);
               break;
            case SliderFormat::Ratio:
               snprintf(buf, len, "%.1f:1", value);
               break;
            case SliderFormat::Pan:
            {
               const int amount = static_cast<int>(std::lround((value - 0.5f) * 200.0f));
               if (amount == 0)
                  snprintf(buf, len, "C");
               else
                  snprintf(buf, len, "%c%d", amount < 0 ? 'L' : 'R', std::abs(amount));
               break;
            }
            case SliderFormat::Choice:
            {
               const int index = static_cast<int>(std::lround(value));
               if (index >= 0 && index < static_cast<int>(spec.choices.size()))
                  snprintf(buf, len, "%s", spec.choices[static_cast<size_t>(index)]);
               else
                  snprintf(buf, len, "%d", index);
               break;
            }
         }
      }

      void SliderPanelModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         const float screenX = (mX + offsetX) * scale;
         const float screenY = (mY + offsetY) * scale;
         const float font = UITheme::kValueFontSize * scale;
         const float barX = screenX + kSidePad * scale;
         const float barW = (mWidth - 2.0f * kSidePad) * scale;
         const Color track(0.2f, 0.2f, 0.22f, 1.0f);
         const Color fill(0.8f, 0.5f, 0.2f, 0.85f);

         for (size_t row = 0; row < mSpecs.size(); ++row)
         {
            const SliderSpec& spec = mSpecs[row];
            const float barY = screenY + (slidersTop() + static_cast<float>(row) * kRowStep) * scale;
            const float barH = kRowHeight * scale;
            const float range = spec.maxValue - spec.minValue;
            const float t = range > 0.0f ? (mValues[row] - spec.minValue) / range : 0.0f;
            renderer.drawSlider(barX, barY, barW, barH, std::max(0.0f, std::min(1.0f, t)), track, fill);

            char valueText[32];
            formatValue(static_cast<int>(row), valueText, sizeof(valueText));
            char line[64];
            snprintf(line, sizeof(line), "%s %s", spec.label, valueText);
            drawText(renderer, barX + 4.0f * scale, textBaselineCentered(barY + barH * 0.5f, font),
                     line, UITheme::kTextPrimary, font);
         }
      }

      void SliderPanelModule::setControlValue(const std::string& name, float value)
      {
         for (size_t row = 0; row < mSpecs.size(); ++row)
         {
            if (name == mSpecs[row].name)
            {
               mValues[row] = snap(static_cast<int>(row), value);
               return;
            }
         }
      }

      float SliderPanelModule::getControlValue(const std::string& name) const
      {
         for (size_t row = 0; row < mSpecs.size(); ++row)
         {
            if (name == mSpecs[row].name)
               return mValues[row];
         }
         return 0.0f;
      }

      bool SliderPanelModule::handleMouseDown(float worldX, float worldY)
      {
         const int row = rowAt(worldX, worldY);
         if (row < 0)
            return false;
         mDragRow = row;
         setFromWorldX(row, worldX);
         return true;
      }

      bool SliderPanelModule::handleMouseDrag(float worldX, float worldY, float dx, float dy)
      {
         (void)worldY;
         (void)dx;
         (void)dy;
         if (mDragRow < 0)
            return false;
         setFromWorldX(mDragRow, worldX);
         return true;
      }

      void SliderPanelModule::handleMouseUp()
      {
         mDragRow = -1;
      }

   } // namespace wasm
} // namespace bespoke
