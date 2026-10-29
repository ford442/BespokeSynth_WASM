/**
 * BespokeSynth WASM - Module help (spawn-menu descriptions, control hover tooltips)
 *
 * The table is baked from resource/tooltips_*.txt by scripts/bake_module_help.py into
 * /resource/help/module_help.json and loaded on first use. Lookups try the active locale,
 * then the table's fallback ("en").
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <map>
#include <string>
#include <string_view>

namespace bespoke
{
   namespace wasm
   {
      class ModuleHelp
      {
      public:
         static constexpr const char* kDefaultPath = "/resource/help/module_help.json";

         static ModuleHelp& instance();

         /** Replace the table with baked JSON; false (table left empty) on a parse error. */
         bool loadFromJson(const std::string& json);
         /** BCP 47 tag such as "de-DE"; only the language subtag is kept ("de"). */
         void setLocale(std::string_view tag);
         const std::string& locale() const { return mLocale; }

         /** "" when the type has no description in the locale or the fallback. */
         std::string description(std::string_view type) const;
         /** "" when the control has no tooltip in the locale or the fallback. */
         std::string controlTooltip(std::string_view type, std::string_view control) const;

         /** Types in the fallback locale (tests use it to check coverage). */
         size_t typeCount() const;

      private:
         struct Entry
         {
            std::string description;
            std::map<std::string, std::string, std::less<>> controls;
         };
         using Table = std::map<std::string, Entry, std::less<>>;

         void ensureLoaded() const;
         const Entry* find(std::string_view locale, std::string_view type) const;

         std::map<std::string, Table, std::less<>> mLocales;
         std::string mFallback = "en";
         std::string mLocale = "en";
         mutable bool mLoadAttempted = false;
      };

   } // namespace wasm
} // namespace bespoke
