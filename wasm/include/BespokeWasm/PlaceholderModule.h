/**
 * BespokeSynth WASM - Placeholder for a module type this build does not have
 *
 * A patch that names a type the adapter registry lacks (a desktop-only module, or one
 * from a newer build) loads this instead of being dropped or aliased onto another
 * module (ADR: docs/wasm-module-porting.md). It has no ports and no DSP. It keeps the
 * original type name, position, controls and extras, and the canvas keeps the cables
 * that touched it, so saving writes the patch back as it came in.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/Module.h"
#include <map>
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class PlaceholderModule : public Module
      {
      public:
         /** Never registered: the canvas creates it only when loading or restoring a patch. */
         static constexpr const char* kTypeId = "placeholder";

         PlaceholderModule(int id, const std::string& originalType);

         void render(Renderer2D& renderer, float offsetX, float offsetY, float scale) override;
         void setControlValue(const std::string& name, float value) override;
         float getControlValue(const std::string& name) const override;
         void setStringProperty(const std::string& name, const std::string& value) override;
         std::string getStringProperty(const std::string& name) const override;
         std::vector<std::string> stringPropertyNames() const override;

         std::string serializedType() const override { return mOriginalType; }
         std::vector<std::string> preservedControlNames() const override;
         bool isPlaceholder() const override { return true; }

         const std::string& originalType() const { return mOriginalType; }
         /** Cables from the patch that could not be connected; shown as a badge. */
         void setKeptCableCount(int count) { mKeptCables = count; }
         int keptCableCount() const { return mKeptCables; }

      private:
         std::string mOriginalType;
         std::map<std::string, float> mControls;
         std::map<std::string, std::string> mExtras;
         int mKeptCables = 0;
      };

   } // namespace wasm
} // namespace bespoke
