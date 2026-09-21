/**
 * BespokeSynth WASM - Script canvas module UI
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      ScriptModule::ScriptModule(int id)
      : Module(id, "script", "Script", ModuleCategory::Other)
      {
         setSize(170, 84);
      }

      void ScriptModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         const float screenX = (mX + offsetX) * scale;
         const float screenY = (mY + offsetY) * scale;
         const float contentTop = screenY + (kTitleBarHeight + 16) * scale;
         const float font = UITheme::kValueFontSize * scale;
         const float textX = screenX + 10 * scale;

         const char* status = mStatus.empty() ? "stopped" : mStatus.c_str();
         const bool running = mStatus == "running";
         char line[64];
         snprintf(line, sizeof(line), "JS %.40s", status);
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, font, scale), line,
                  running ? UITheme::kTextValue : UITheme::kTextSecondary, font);

         // First non-empty source line as a preview.
         std::string preview = "(empty - open Script panel)";
         size_t start = 0;
         while (start < mScript.size())
         {
            size_t end = mScript.find('\n', start);
            if (end == std::string::npos)
               end = mScript.size();
            const std::string candidate = mScript.substr(start, end - start);
            if (candidate.find_first_not_of(" \t\r") != std::string::npos)
            {
               preview = candidate;
               break;
            }
            start = end + 1;
         }
         char clipped[64];
         truncateWithEllipsis(renderer, preview.c_str(), (mWidth - 20.0f) * scale, clipped, sizeof(clipped));
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, font, scale), clipped,
                  UITheme::kTextSecondary, font);
      }

      void ScriptModule::setStringProperty(const std::string& name, const std::string& value)
      {
         if (name == "script")
            mScript = value;
         else if (name == "status")
            mStatus = value;
      }

      std::string ScriptModule::getStringProperty(const std::string& name) const
      {
         if (name == "script")
            return mScript;
         if (name == "status")
            return mStatus;
         return {};
      }

      std::vector<std::string> ScriptModule::stringPropertyNames() const
      {
         // status is runtime-only; scripts never auto-run from a loaded patch.
         return { "script" };
      }

   }
}
