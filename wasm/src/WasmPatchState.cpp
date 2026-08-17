/**
 * BespokeSynth WASM - Patch state JSON serialization
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmPatchState.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         struct JsonValue
         {
            enum class Type
            {
               Null,
               Bool,
               Number,
               String,
               Array,
               Object
            };

            Type type = Type::Null;
            bool boolValue = false;
            double numberValue = 0.0;
            std::string stringValue;
            std::vector<JsonValue> arrayValue;
            std::map<std::string, JsonValue> objectValue;

            const JsonValue* get(const std::string& key) const
            {
               auto it = objectValue.find(key);
               return it == objectValue.end() ? nullptr : &it->second;
            }
         };

         class JsonParser
         {
         public:
            explicit JsonParser(const std::string& text)
            : mText(text)
            {
            }

            bool parse(JsonValue& out, std::string& error)
            {
               skipWhitespace();
               if (!parseValue(out, error))
                  return false;
               skipWhitespace();
               if (mPos != mText.size())
               {
                  error = "Unexpected trailing data";
                  return false;
               }
               return true;
            }

         private:
            void skipWhitespace()
            {
               while (mPos < mText.size() && std::isspace(static_cast<unsigned char>(mText[mPos])))
                  ++mPos;
            }

            bool consume(char expected)
            {
               skipWhitespace();
               if (mPos >= mText.size() || mText[mPos] != expected)
                  return false;
               ++mPos;
               return true;
            }

            bool parseValue(JsonValue& out, std::string& error)
            {
               skipWhitespace();
               if (mPos >= mText.size())
               {
                  error = "Unexpected end of JSON";
                  return false;
               }

               const char c = mText[mPos];
               if (c == '{')
                  return parseObject(out, error);
               if (c == '[')
                  return parseArray(out, error);
               if (c == '"')
                  return parseStringValue(out, error);
               if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
                  return parseNumber(out, error);
               if (mText.compare(mPos, 4, "true") == 0)
               {
                  mPos += 4;
                  out.type = JsonValue::Type::Bool;
                  out.boolValue = true;
                  return true;
               }
               if (mText.compare(mPos, 5, "false") == 0)
               {
                  mPos += 5;
                  out.type = JsonValue::Type::Bool;
                  out.boolValue = false;
                  return true;
               }
               if (mText.compare(mPos, 4, "null") == 0)
               {
                  mPos += 4;
                  out.type = JsonValue::Type::Null;
                  return true;
               }

               error = "Unexpected JSON token";
               return false;
            }

            bool parseObject(JsonValue& out, std::string& error)
            {
               if (!consume('{'))
                  return false;
               out.type = JsonValue::Type::Object;
               skipWhitespace();
               if (consume('}'))
                  return true;

               while (true)
               {
                  JsonValue key;
                  if (!parseStringValue(key, error))
                     return false;
                  if (!consume(':'))
                  {
                     error = "Expected ':' after object key";
                     return false;
                  }
                  JsonValue value;
                  if (!parseValue(value, error))
                     return false;
                  out.objectValue[key.stringValue] = value;

                  if (consume('}'))
                     return true;
                  if (!consume(','))
                  {
                     error = "Expected ',' or '}' in object";
                     return false;
                  }
               }
            }

            bool parseArray(JsonValue& out, std::string& error)
            {
               if (!consume('['))
                  return false;
               out.type = JsonValue::Type::Array;
               skipWhitespace();
               if (consume(']'))
                  return true;

               while (true)
               {
                  JsonValue value;
                  if (!parseValue(value, error))
                     return false;
                  out.arrayValue.push_back(value);

                  if (consume(']'))
                     return true;
                  if (!consume(','))
                  {
                     error = "Expected ',' or ']' in array";
                     return false;
                  }
               }
            }

            bool parseStringValue(JsonValue& out, std::string& error)
            {
               skipWhitespace();
               if (mPos >= mText.size() || mText[mPos] != '"')
               {
                  error = "Expected string";
                  return false;
               }
               ++mPos;
               out.type = JsonValue::Type::String;
               out.stringValue.clear();

               while (mPos < mText.size())
               {
                  const char c = mText[mPos++];
                  if (c == '"')
                     return true;
                  if (c != '\\')
                  {
                     out.stringValue.push_back(c);
                     continue;
                  }
                  if (mPos >= mText.size())
                  {
                     error = "Unterminated escape sequence";
                     return false;
                  }
                  const char esc = mText[mPos++];
                  switch (esc)
                  {
                     case '"':
                     case '\\':
                     case '/':
                        out.stringValue.push_back(esc);
                        break;
                     case 'b':
                        out.stringValue.push_back('\b');
                        break;
                     case 'f':
                        out.stringValue.push_back('\f');
                        break;
                     case 'n':
                        out.stringValue.push_back('\n');
                        break;
                     case 'r':
                        out.stringValue.push_back('\r');
                        break;
                     case 't':
                        out.stringValue.push_back('\t');
                        break;
                     default:
                        error = "Unsupported string escape";
                        return false;
                  }
               }

               error = "Unterminated string";
               return false;
            }

            bool parseNumber(JsonValue& out, std::string& error)
            {
               skipWhitespace();
               const size_t start = mPos;
               if (mText[mPos] == '-')
                  ++mPos;
               while (mPos < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPos])))
                  ++mPos;
               if (mPos < mText.size() && mText[mPos] == '.')
               {
                  ++mPos;
                  while (mPos < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPos])))
                     ++mPos;
               }
               if (mPos < mText.size() && (mText[mPos] == 'e' || mText[mPos] == 'E'))
               {
                  ++mPos;
                  if (mPos < mText.size() && (mText[mPos] == '-' || mText[mPos] == '+'))
                     ++mPos;
                  while (mPos < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPos])))
                     ++mPos;
               }

               try
               {
                  out.type = JsonValue::Type::Number;
                  out.numberValue = std::stod(mText.substr(start, mPos - start));
                  return true;
               }
               catch (...)
               {
                  error = "Invalid number";
                  return false;
               }
            }

            const std::string& mText;
            size_t mPos = 0;
         };

         std::string escapeJson(const std::string& value)
         {
            std::ostringstream out;
            for (char c : value)
            {
               switch (c)
               {
                  case '"':
                     out << "\\\"";
                     break;
                  case '\\':
                     out << "\\\\";
                     break;
                  case '\n':
                     out << "\\n";
                     break;
                  case '\r':
                     out << "\\r";
                     break;
                  case '\t':
                     out << "\\t";
                     break;
                  default:
                     out << c;
                     break;
               }
            }
            return out.str();
         }

         double numberOr(const JsonValue* value, double fallback)
         {
            return value && value->type == JsonValue::Type::Number ? value->numberValue : fallback;
         }

         bool boolOr(const JsonValue* value, bool fallback)
         {
            return value && value->type == JsonValue::Type::Bool ? value->boolValue : fallback;
         }

         std::string stringOr(const JsonValue* value, const std::string& fallback)
         {
            return value && value->type == JsonValue::Type::String ? value->stringValue : fallback;
         }

         int intOr(const JsonValue* value, int fallback)
         {
            return static_cast<int>(numberOr(value, fallback));
         }

         const JsonValue* objectChild(const JsonValue* value, const std::string& key)
         {
            return value && value->type == JsonValue::Type::Object ? value->get(key) : nullptr;
         }

         int nestedIntOr(const JsonValue& object,
                         const std::string& objectKey,
                         const std::string& valueKey,
                         const std::string& flatKey,
                         int fallback)
         {
            if (const JsonValue* nested = objectChild(object.get(objectKey), valueKey))
               return intOr(nested, fallback);
            return intOr(object.get(flatKey), fallback);
         }
      }

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
         out << "  ]\n";
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

         snapshot = std::move(parsed);
         migratePatchState(snapshot);
         return true;
      }

      void migratePatchState(ModuleCanvas::StateSnapshot& snapshot)
      {
         if (snapshot.schemaVersion >= kPatchSchemaVersion)
            return;

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
         snapshot.schemaVersion = kPatchSchemaVersion;
      }
   } // namespace wasm
} // namespace bespoke
