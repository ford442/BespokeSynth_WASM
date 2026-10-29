/**
 * BespokeSynth WASM - Patch state JSON serialization
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmPatchState.h"
#include "BespokeWasm/WasmJson.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include "BespokeWasm/MidiMap.h"
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>

namespace bespoke
{
   namespace wasm
   {
      using namespace json;

      std::string serializePatchState(const ModuleCanvas::StateSnapshot& snapshot, int viewMode)
      {
         std::ostringstream out;
         out << std::fixed << std::setprecision(6);
         out << "{\n";
         out << "  \"filetype\": \"bespokesynth\",\n";
         out << "  \"format\": \"bespokesynth-wasm-state\",\n";
         out << "  \"version\": " << snapshot.schemaVersion << ",\n";
         out << "  \"wasm\": {\n";
         out << "    \"view_mode\": " << viewMode << ",\n";
         out << "    \"canvas\": { \"offset_x\": " << snapshot.offsetX
             << ", \"offset_y\": " << snapshot.offsetY
             << ", \"scale\": " << snapshot.scale << " }\n";
         out << "  },\n";
         out << "  \"transport\": { \"bpm\": " << snapshot.transportBPM
             << ", \"playing\": " << (snapshot.transportPlaying ? "true" : "false") << " },\n";
         out << "  \"modules\": [\n";
         for (size_t i = 0; i < snapshot.modules.size(); ++i)
         {
            const auto& module = snapshot.modules[i];
            out << "    {\n";
            out << "      \"id\": " << module.id << ",\n";
            out << "      \"type\": \"" << escapeJson(module.type) << "\",\n";
            out << "      \"name\": \"" << escapeJson(module.type) << "\",\n";
            out << "      \"position\": { \"x\": " << module.x << ", \"y\": " << module.y << " },\n";
            out << "      \"minimized\": " << (module.minimized ? "true" : "false") << ",\n";
            out << "      \"enabled\": " << (module.enabled ? "true" : "false") << ",\n";
            out << "      \"controls\": {";
            size_t controlIndex = 0;
            for (const auto& [name, value] : module.controls)
            {
               out << (controlIndex++ == 0 ? " " : ", ");
               out << "\"" << escapeJson(name) << "\": " << value;
            }
            out << (module.controls.empty() ? "" : " ") << "}";
            if (!module.extras.empty())
            {
               out << ",\n      \"extras\": {";
               size_t extraIndex = 0;
               for (const auto& [name, value] : module.extras)
               {
                  out << (extraIndex++ == 0 ? " " : ", ");
                  out << "\"" << escapeJson(name) << "\": \"" << escapeJson(value) << "\"";
               }
               out << " }";
            }
            out << "\n";
            out << "    }" << (i + 1 < snapshot.modules.size() ? "," : "") << "\n";
         }
         out << "  ],\n";
         out << "  \"connections\": [\n";
         for (size_t i = 0; i < snapshot.connections.size(); ++i)
         {
            const auto& conn = snapshot.connections[i];
            out << "    { \"source\": { \"module\": " << conn.sourceModuleId
                << ", \"port\": " << conn.sourcePortIndex
                << " }, \"destination\": { \"module\": " << conn.destModuleId
                << ", \"port\": " << conn.destPortIndex
                << " }, \"source_module_id\": " << conn.sourceModuleId
                << ", \"source_port_index\": " << conn.sourcePortIndex
                << ", \"dest_module_id\": " << conn.destModuleId
                << ", \"dest_port_index\": " << conn.destPortIndex
                << " }" << (i + 1 < snapshot.connections.size() ? "," : "") << "\n";
         }
         out << "  ],\n";
         out << "  \"midi\": {\n";
         out << "    \"note_channel\": " << snapshot.midiNoteChannel << ",\n";
         out << "    \"cc\": [\n";
         for (size_t i = 0; i < snapshot.midiCcMap.size(); ++i)
         {
            const auto& binding = snapshot.midiCcMap[i];
            out << "      { \"channel\": " << binding.channel
                << ", \"cc\": " << binding.cc
                << ", \"module_id\": " << binding.moduleId
                << ", \"control\": \"" << escapeJson(binding.controlName) << "\""
                << ", \"min\": " << binding.min
                << ", \"max\": " << binding.max
                << " }" << (i + 1 < snapshot.midiCcMap.size() ? "," : "") << "\n";
         }
         out << "    ]\n";
         out << "  }\n";
         out << "}\n";
         return out.str();
      }

      bool deserializePatchState(const std::string& json,
                                 ModuleCanvas::StateSnapshot& snapshot,
                                 int& viewMode,
                                 std::string& error)
      {
         JsonValue root;
         JsonParser parser(json);
         if (!parser.parse(root, error))
            return false;
         if (root.type != JsonValue::Type::Object)
         {
            error = "Patch state root must be a JSON object";
            return false;
         }

         ModuleCanvas::StateSnapshot parsed;
         parsed.schemaVersion = intOr(root.get("version"), 1);

         const JsonValue* wasm = root.get("wasm");
         viewMode = intOr(objectChild(wasm, "view_mode"), viewMode);
         const JsonValue* canvas = objectChild(wasm, "canvas");
         parsed.offsetX = static_cast<float>(numberOr(objectChild(canvas, "offset_x"), 0.0));
         parsed.offsetY = static_cast<float>(numberOr(objectChild(canvas, "offset_y"), 0.0));
         parsed.scale = static_cast<float>(numberOr(objectChild(canvas, "scale"), 1.0));

         const JsonValue* transport = root.get("transport");
         parsed.transportBPM = static_cast<float>(numberOr(objectChild(transport, "bpm"), 120.0));
         parsed.transportPlaying = boolOr(objectChild(transport, "playing"), false);

         const JsonValue* modules = root.get("modules");
         if (!modules || modules->type != JsonValue::Type::Array)
         {
            error = "Patch state missing modules array";
            return false;
         }

         for (const auto& moduleValue : modules->arrayValue)
         {
            if (moduleValue.type != JsonValue::Type::Object)
               continue;

            ModuleCanvas::StateModule module;
            module.id = intOr(moduleValue.get("id"), -1);
            module.type = stringOr(moduleValue.get("type"), "");
            const JsonValue* position = moduleValue.get("position");
            module.x = static_cast<float>(numberOr(objectChild(position, "x"), 0.0));
            module.y = static_cast<float>(numberOr(objectChild(position, "y"), 0.0));
            module.minimized = boolOr(moduleValue.get("minimized"), false);
            module.enabled = boolOr(moduleValue.get("enabled"), true);

            const JsonValue* controls = moduleValue.get("controls");
            if (controls && controls->type == JsonValue::Type::Object)
            {
               for (const auto& [name, value] : controls->objectValue)
               {
                  if (value.type == JsonValue::Type::Number)
                     module.controls[name] = static_cast<float>(value.numberValue);
               }
            }

            const JsonValue* extras = moduleValue.get("extras");
            if (extras && extras->type == JsonValue::Type::Object)
            {
               for (const auto& [name, value] : extras->objectValue)
               {
                  if (value.type == JsonValue::Type::String)
                     module.extras[name] = value.stringValue;
               }
            }

            if (module.id >= 0 && !module.type.empty())
               parsed.modules.push_back(module);
         }

         const JsonValue* connections = root.get("connections");
         if (connections && connections->type == JsonValue::Type::Array)
         {
            for (const auto& connValue : connections->arrayValue)
            {
               if (connValue.type != JsonValue::Type::Object)
                  continue;

               ModuleCanvas::StateConnection conn;
               conn.sourceModuleId = nestedIntOr(connValue, "source", "module", "source_module_id", -1);
               conn.sourcePortIndex = nestedIntOr(connValue, "source", "port", "source_port_index", 0);
               conn.destModuleId = nestedIntOr(connValue, "destination", "module", "dest_module_id", -1);
               conn.destPortIndex = nestedIntOr(connValue, "destination", "port", "dest_port_index", 0);
               if (conn.sourceModuleId >= 0 && conn.destModuleId >= 0)
                  parsed.connections.push_back(conn);
            }
         }

         const JsonValue* midi = root.get("midi");
         if (midi && midi->type == JsonValue::Type::Object)
         {
            parsed.midiNoteChannel = intOr(midi->get("note_channel"), -1);
            const JsonValue* ccList = midi->get("cc");
            if (ccList && ccList->type == JsonValue::Type::Array)
            {
               for (const auto& bindingValue : ccList->arrayValue)
               {
                  if (bindingValue.type != JsonValue::Type::Object)
                     continue;
                  MidiCcBinding binding;
                  binding.channel = intOr(bindingValue.get("channel"), -1);
                  binding.cc = intOr(bindingValue.get("cc"), 0);
                  binding.moduleId = intOr(bindingValue.get("module_id"), -1);
                  binding.controlName = stringOr(bindingValue.get("control"), "");
                  binding.min = static_cast<float>(numberOr(bindingValue.get("min"), 0.0));
                  binding.max = static_cast<float>(numberOr(bindingValue.get("max"), 1.0));
                  if (binding.cc >= 0 && binding.moduleId >= 0 && !binding.controlName.empty())
                     parsed.midiCcMap.push_back(std::move(binding));
               }
            }
         }

         snapshot = std::move(parsed);
         migratePatchState(snapshot);
         return true;
      }

      void seedDefaultMidiCcBindings(ModuleCanvas::StateSnapshot& snapshot)
      {
         auto findType = [&snapshot](const char* type) -> int
         {
            for (const auto& module : snapshot.modules)
            {
               if (module.type == type)
                  return module.id;
            }
            return -1;
         };

         snapshot.midiCcMap.clear();
         const int gain = findType("gain");
         if (gain >= 0)
            snapshot.midiCcMap.push_back({ -1, 7, gain, "gain", 0.0f, 1.0f });
         const int filter = findType("filter");
         if (filter >= 0)
            snapshot.midiCcMap.push_back({ -1, 74, filter, "cutoff", 20.0f, 20000.0f });
         const int lfo = findType("lfo");
         if (lfo >= 0)
            snapshot.midiCcMap.push_back({ -1, 1, lfo, "depth", 0.0f, 1.0f });
      }

      void migratePatchState(ModuleCanvas::StateSnapshot& snapshot)
      {
         // v5 turned the Output DC blocker on by default; older patches keep their exact sound.
         if (snapshot.schemaVersion < 5)
         {
            for (auto& module : snapshot.modules)
            {
               if (module.type == "output" && module.controls.find("dcblock") == module.controls.end())
                  module.controls["dcblock"] = 0.0f;
            }
         }
         // Additive controls (mixer pan1..4, output limit, chorus width) need no schema bump:
         // any control a saved patch lacks takes its descriptor default, even on current-version patches.
         auto& registry = WasmModuleAdapterRegistry::instance();
         for (auto& module : snapshot.modules)
         {
            const WasmModuleAdapter* adapter = registry.find(module.type);
            if (!adapter)
               continue;
            for (const auto& control : adapter->controlDescriptors())
            {
               if (module.controls.find(control.name) == module.controls.end())
                  module.controls[control.name] = control.defaultValue;
            }
         }
         if (snapshot.schemaVersion >= kPatchSchemaVersion)
            return;
         if (snapshot.schemaVersion < 4)
            seedDefaultMidiCcBindings(snapshot);
         snapshot.schemaVersion = kPatchSchemaVersion;
      }
   } // namespace wasm
} // namespace bespoke
