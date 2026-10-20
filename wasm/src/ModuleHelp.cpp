/**
 * BespokeSynth WASM - Module help table
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleHelp.h"
#include "BespokeWasm/WasmJson.h"
#include <cctype>
#include <fstream>
#include <sstream>

namespace bespoke
{
   namespace wasm
   {
      ModuleHelp& ModuleHelp::instance()
      {
         static ModuleHelp help;
         return help;
      }

      bool ModuleHelp::loadFromJson(const std::string& text)
      {
         mLoadAttempted = true;
         mLocales.clear();
         json::JsonValue root;
         std::string error;
         json::JsonParser parser(text);
         if (!parser.parse(root, error) || root.type != json::JsonValue::Type::Object)
            return false;
         mFallback = json::stringOr(root.get("fallback"), "en");
         const json::JsonValue* locales = root.get("locales");
         if (!locales || locales->type != json::JsonValue::Type::Object)
            return false;
         for (const auto& [code, types] : locales->objectValue)
         {
            Table& table = mLocales[code];
            for (const auto& [type, value] : types.objectValue)
            {
               Entry& entry = table[type];
               entry.description = json::stringOr(value.get("d"), "");
               if (const json::JsonValue* controls = value.get("c"))
               {
                  for (const auto& [control, tooltip] : controls->objectValue)
                     entry.controls[control] = tooltip.stringValue;
               }
            }
         }
         return true;
      }

      void ModuleHelp::setLocale(std::string_view tag)
      {
         std::string language;
         for (char c : tag)
         {
            if (c == '-' || c == '_')
               break;
            language.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
         }
         mLocale = language.empty() ? mFallback : language;
      }

      void ModuleHelp::ensureLoaded() const
      {
         if (mLoadAttempted)
            return;
         mLoadAttempted = true;
         std::ifstream file(kDefaultPath, std::ios::binary);
         if (!file)
            return;
         std::ostringstream text;
         text << file.rdbuf();
         // Lazily filled on the first lookup; the table is otherwise read-only.
         const_cast<ModuleHelp*>(this)->loadFromJson(text.str());
      }

      const ModuleHelp::Entry* ModuleHelp::find(std::string_view locale, std::string_view type) const
      {
         auto table = mLocales.find(locale);
         if (table == mLocales.end())
            return nullptr;
         auto entry = table->second.find(type);
         return entry == table->second.end() ? nullptr : &entry->second;
      }

      std::string ModuleHelp::description(std::string_view type) const
      {
         ensureLoaded();
         for (std::string_view locale : { std::string_view(mLocale), std::string_view(mFallback) })
         {
            if (const Entry* entry = find(locale, type); entry && !entry->description.empty())
               return entry->description;
         }
         return {};
      }

      std::string ModuleHelp::controlTooltip(std::string_view type, std::string_view control) const
      {
         ensureLoaded();
         for (std::string_view locale : { std::string_view(mLocale), std::string_view(mFallback) })
         {
            if (const Entry* entry = find(locale, type))
            {
               auto it = entry->controls.find(control);
               if (it != entry->controls.end() && !it->second.empty())
                  return it->second;
            }
         }
         return {};
      }

      size_t ModuleHelp::typeCount() const
      {
         ensureLoaded();
         auto table = mLocales.find(mFallback);
         return table == mLocales.end() ? 0 : table->second.size();
      }

   } // namespace wasm
} // namespace bespoke
