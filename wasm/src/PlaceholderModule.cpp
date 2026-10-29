/**
 * BespokeSynth WASM - Placeholder for a module type this build does not have
 */

#include "BespokeWasm/PlaceholderModule.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      PlaceholderModule::PlaceholderModule(int id, const std::string& originalType)
      : Module(id, kTypeId, originalType, ModuleCategory::Other)
      , mOriginalType(originalType)
      {
         setSize(176.0f, kTitleBarHeight + 46.0f);
      }

      void PlaceholderModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         const float screenX = (mX + offsetX) * scale;
         const float screenY = (mY + offsetY) * scale;
         const float font = UITheme::kValueFontSize * scale;
         const float x = screenX + 8.0f * scale;
         float top = screenY + (kTitleBarHeight + 6.0f) * scale;

         drawText(renderer, x, textBaselineFromTop(renderer, top), "Not in this build", UITheme::kAccentAmber, font);
         top += 14.0f * scale;
         drawText(renderer, x, textBaselineFromTop(renderer, top), "Saved back unchanged", UITheme::kTextSecondary, font);
         if (mKeptCables > 0)
         {
            top += 14.0f * scale;
            char line[48];
            snprintf(line, sizeof(line), "%d cable%s kept, not connected", mKeptCables, mKeptCables == 1 ? "" : "s");
            drawText(renderer, x, textBaselineFromTop(renderer, top), line, UITheme::kTextSecondary, font);
         }
      }

      void PlaceholderModule::setControlValue(const std::string& name, float value)
      {
         mControls[name] = value;
      }

      float PlaceholderModule::getControlValue(const std::string& name) const
      {
         auto it = mControls.find(name);
         return it == mControls.end() ? 0.0f : it->second;
      }

      void PlaceholderModule::setStringProperty(const std::string& name, const std::string& value)
      {
         mExtras[name] = value;
         // A desktop import keeps the module's name; the title bar shows it.
         if (name == "label" && !value.empty())
            mName = value;
      }

      std::string PlaceholderModule::getStringProperty(const std::string& name) const
      {
         auto it = mExtras.find(name);
         return it == mExtras.end() ? std::string() : it->second;
      }

      std::vector<std::string> PlaceholderModule::stringPropertyNames() const
      {
         std::vector<std::string> names;
         names.reserve(mExtras.size());
         for (const auto& [name, value] : mExtras)
            names.push_back(name);
         return names;
      }

      std::vector<std::string> PlaceholderModule::preservedControlNames() const
      {
         std::vector<std::string> names;
         names.reserve(mControls.size());
         for (const auto& [name, value] : mControls)
            names.push_back(name);
         return names;
      }

   } // namespace wasm
} // namespace bespoke
