/**
 * BespokeSynth WASM - Module canvas shared types
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/Renderer2D.h"
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      enum class ModuleCategory
      {
         Instrument,
         NoteEffect,
         Synth,
         AudioEffect,
         Modulator,
         Pulse,
         Other
      };

      enum class PortType
      {
         Audio,
         Note,
         Pulse,
         Modulation
      };

      enum class WasmAudioRole
      {
         None,
         AudioSource,
         AudioProcessor,
         ModulationSource,
         NoteSource,
         Sink
      };

      struct Port
      {
         std::string name;
         PortType type;
         bool isOutput;
         float x, y;

         Port(const std::string& name, PortType type, bool isOutput)
         : name(name)
         , type(type)
         , isOutput(isOutput)
         , x(0)
         , y(0)
         {
         }
      };

      struct Connection
      {
         int sourceModuleId;
         int sourcePortIndex;
         int destModuleId;
         int destPortIndex;
         Color color;

         Connection()
         : sourceModuleId(-1)
         , sourcePortIndex(0)
         , destModuleId(-1)
         , destPortIndex(0)
         {
         }
      };

      struct ModuleTypeInfo
      {
         std::string type;
         std::string displayName;
         ModuleCategory category;
      };

   } // namespace wasm
} // namespace bespoke
