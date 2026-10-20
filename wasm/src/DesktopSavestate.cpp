/**
 * BespokeSynth WASM - Desktop savestate (.bsk) import, layout subset
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/DesktopSavestate.h"
#include "BespokeWasm/WasmJson.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <optional>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         struct TypeMapping
         {
            const char* desktop;
            const char* wasm;
         };

         // Desktop module (and effect) type names whose WASM adapter does the same job.
         // Desktop "lfo" (a slider-attached LFO) and "script" (Python) only share a name with
         // the WASM modules: they load as placeholders.
         constexpr TypeMapping kTypeMap[] = {
            { "arpeggiator", "arpeggiator" },
            { "biquad", "filter" },
            { "bitcrush", "bitcrush" },
            { "capo", "capo" },
            { "chorder", "chorder" },
            { "compressor", "compressor" },
            { "delay", "delay" },
            { "distortion", "distortion" },
            { "drumsequencer", "drumseq" },
            { "envelope", "envelope" },
            { "eq", "eq" },
            { "fftvocoder", "vocoder" },
            { "fmsynth", "fmsynth" },
            { "freeverb", "reverb" },
            { "gain", "gain" },
            { "input", "input" },
            { "karplusstrong", "karplusstrong" },
            { "looper", "looper" },
            { "notechance", "notechance" },
            { "notefilter", "notefilter" },
            { "noterouter", "noterouter" },
            { "notesequencer", "notesequencer" },
            { "oscillator", "oscillator" },
            { "output", "output" },
            { "panner", "panner" },
            { "pitchbender", "pitchbender" },
            { "ramper", "ramper" },
            { "reverb", "reverb" },
            { "sampler", "sampler" },
            { "scale", "scale" },
            { "seaofgrain", "seaofgrain" },
            { "send", "send" },
            { "transport", "transport" },
            { "vocoder", "vocoder" },
            { "waveshaper", "waveshaper" },
         };

         // Prefix for an unsupported desktop type that is also a WASM type id, so the
         // placeholder survives a reload instead of turning into the WASM module.
         constexpr const char* kCollisionPrefix = "desktop:";

         enum class AliasKind
         {
            Number, // copied, clamped to the WASM range
            VoiceLimit, // desktop -1 means "no limit": keep the WASM default
            OscWave, // desktop OscillatorType to the WASM waveform order
         };

         struct ParamAlias
         {
            const char* wasmType;
            const char* desktopKey;
            const char* control;
            AliasKind kind;
         };

         // Layout-JSON values the desktop saves outside the module's binary state.
         constexpr ParamAlias kParamAliases[] = {
            { "fmsynth", "voicelimit", "voices", AliasKind::VoiceLimit },
            { "karplusstrong", "voicelimit", "voices", AliasKind::VoiceLimit },
            { "seaofgrain", "voicelimit", "voices", AliasKind::VoiceLimit },
            { "oscillator", "vol", "volume", AliasKind::Number },
            { "oscillator", "osc", "waveform", AliasKind::OscWave },
            { "notesequencer", "gridsteps", "steps", AliasKind::Number },
            { "notesequencer", "gridrows", "rows", AliasKind::Number },
         };

         bool readU64(const uint8_t* bytes, size_t length, size_t& pos, uint64_t& value)
         {
            if (pos + 8 > length)
               return false;
            uint64_t v = 0;
            for (int i = 7; i >= 0; --i)
               v = (v << 8) | bytes[pos + static_cast<size_t>(i)];
            value = v;
            pos += 8;
            return true;
         }

         bool readU32(const uint8_t* bytes, size_t length, size_t& pos, uint32_t& value)
         {
            if (pos + 4 > length)
               return false;
            value = static_cast<uint32_t>(bytes[pos]) | (static_cast<uint32_t>(bytes[pos + 1]) << 8) |
                    (static_cast<uint32_t>(bytes[pos + 2]) << 16) | (static_cast<uint32_t>(bytes[pos + 3]) << 24);
            pos += 4;
            return true;
         }

         // FileStreamIn::sMaxStringLength: a longer first "length" means a 32-bit save.
         constexpr uint64_t kMaxStringLength = 999999999;

         bool readString(const uint8_t* bytes, size_t length, size_t& pos, bool mode32, std::string& out)
         {
            uint64_t size = 0;
            if (mode32)
            {
               uint32_t size32 = 0;
               if (!readU32(bytes, length, pos, size32))
                  return false;
               size = size32;
            }
            else if (!readU64(bytes, length, pos, size))
            {
               return false;
            }
            if (size > length - pos)
               return false;
            out.assign(reinterpret_cast<const char*>(bytes + pos), static_cast<size_t>(size));
            pos += static_cast<size_t>(size);
            return true;
         }

         std::string trim(const std::string& text)
         {
            const size_t start = text.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
               return {};
            const size_t end = text.find_last_not_of(" \t\r\n");
            return text.substr(start, end - start + 1);
         }

         const ParamDescriptor* findParam(const WasmModuleAdapter& adapter, const char* name)
         {
            for (const ParamDescriptor& param : adapter.paramTable())
            {
               if (std::strcmp(param.name, name) == 0)
                  return &param;
            }
            return nullptr;
         }

         std::optional<float> aliasValue(const ParamAlias& alias, double desktopValue)
         {
            switch (alias.kind)
            {
               case AliasKind::VoiceLimit:
                  if (desktopValue <= 0.0)
                     return std::nullopt;
                  return static_cast<float>(desktopValue);
               case AliasKind::OscWave:
               {
                  // kOsc_Sin, kOsc_Square, kOsc_Tri, kOsc_Saw -> sine 0, saw 1, square 2, triangle 3.
                  constexpr float kWave[] = { 0.0f, 2.0f, 3.0f, 1.0f };
                  const int desktop = static_cast<int>(desktopValue);
                  if (desktop < 0 || desktop > 3)
                     return std::nullopt;
                  return kWave[desktop];
               }
               case AliasKind::Number:
                  return static_cast<float>(desktopValue);
            }
            return std::nullopt;
         }

         struct ImportedModule
         {
            std::string name;
            const WasmModuleAdapter* adapter = nullptr; // nullptr: placeholder
         };

         /** Input on `dest` a cable from an output of `sourceType` lands on, or -1. */
         int compatibleInput(const WasmModuleAdapter& dest, std::optional<PortType> sourceType, bool controlTarget)
         {
            const auto inputs = dest.inputPorts();
            const std::optional<PortType> wanted = controlTarget ? std::optional<PortType>(PortType::Modulation)
                                                                 : sourceType;
            for (size_t i = 0; i < inputs.size(); ++i)
            {
               if (!wanted || inputs[i].type == *wanted)
                  return static_cast<int>(i);
            }
            return -1;
         }
      }

      const char* wasmTypeForDesktopType(std::string_view desktopType)
      {
         for (const auto& mapping : kTypeMap)
         {
            if (desktopType == mapping.desktop)
               return mapping.wasm;
         }
         return nullptr;
      }

      bool looksLikeDesktopSavestate(const uint8_t* bytes, size_t length)
      {
         // A FileStream string length (little-endian, high bytes zero) rather than JSON text.
         return length >= 8 && bytes[0] != '{' && bytes[4] == 0 && bytes[5] == 0 && bytes[6] == 0 && bytes[7] == 0;
      }

      bool extractDesktopLayoutJson(const uint8_t* bytes, size_t length, std::string& json, int& saveStateRev,
                                    std::string& error)
      {
         saveStateRev = 0;
         size_t pos = 0;
         uint64_t firstLength = 0;
         size_t peek = 0;
         if (!readU64(bytes, length, peek, firstLength))
         {
            error = "File is too short to be a savestate";
            return false;
         }
         const bool mode32 = firstLength >= kMaxStringLength;
         std::string header;
         if (!readString(bytes, length, pos, mode32, header))
         {
            error = "Savestate is truncated";
            return false;
         }
         if (header != "bskfile")
         {
            json = std::move(header); // old files start with the layout
            return true;
         }
         uint32_t rev = 0;
         uint32_t screenshotSize = 0;
         if (!readU32(bytes, length, pos, rev) || !readU32(bytes, length, pos, screenshotSize) ||
             screenshotSize > length - pos)
         {
            error = "Savestate header is truncated";
            return false;
         }
         pos += screenshotSize;
         saveStateRev = static_cast<int>(rev);
         if (!readString(bytes, length, pos, mode32, json))
         {
            error = "Savestate layout is truncated";
            return false;
         }
         return true;
      }

      bool importDesktopLayout(const std::string& text, ModuleCanvas::StateSnapshot& snapshot,
                               DesktopImportReport& report, std::string& error)
      {
         using namespace json;
         JsonValue root;
         JsonParser parser(text);
         if (!parser.parse(root, error))
            return false;
         const JsonValue* modules = root.get("modules");
         if (!modules || modules->type != JsonValue::Type::Array)
         {
            error = "Layout has no modules array";
            return false;
         }

         auto& registry = WasmModuleAdapterRegistry::instance();
         snapshot = ModuleCanvas::StateSnapshot{};
         std::vector<ImportedModule> imported;
         std::map<std::string, int> idByName;
         float minX = 0.0f;
         float minY = 0.0f;
         for (const JsonValue& desktop : modules->arrayValue)
         {
            if (desktop.type != JsonValue::Type::Object)
               continue;
            const std::string desktopType = stringOr(desktop.get("type"), "");
            if (desktopType.empty())
               continue;
            ModuleCanvas::StateModule module;
            module.id = static_cast<int>(snapshot.modules.size()) + 1;
            if (const JsonValue* position = desktop.get("position");
                position && position->arrayValue.size() >= 2)
            {
               module.x = static_cast<float>(numberOr(&position->arrayValue[0], 0.0));
               module.y = static_cast<float>(numberOr(&position->arrayValue[1], 0.0));
            }
            module.minimized = boolOr(desktop.get("minimized"), false);

            ImportedModule entry;
            entry.name = stringOr(desktop.get("name"), desktopType);
            const char* wasmType = wasmTypeForDesktopType(desktopType);
            entry.adapter = wasmType ? registry.find(wasmType) : nullptr;
            if (entry.adapter)
            {
               module.type = wasmType;
               for (const ParamAlias& alias : kParamAliases)
               {
                  const JsonValue* value = desktop.get(alias.desktopKey);
                  const ParamDescriptor* param = findParam(*entry.adapter, alias.control);
                  if (module.type != alias.wasmType || !value || value->type != JsonValue::Type::Number || !param)
                     continue;
                  if (auto mapped = aliasValue(alias, value->numberValue))
                     module.controls[alias.control] = std::clamp(*mapped, param->minValue, param->maxValue);
               }
            }
            else
            {
               module.type = registry.find(desktopType) ? kCollisionPrefix + desktopType : desktopType;
               module.extras["label"] = entry.name;
               ++report.placeholders;
            }

            if (imported.empty())
            {
               minX = module.x;
               minY = module.y;
            }
            minX = std::min(minX, module.x);
            minY = std::min(minY, module.y);
            idByName[entry.name] = module.id;
            imported.push_back(std::move(entry));
            snapshot.modules.push_back(std::move(module));
         }
         report.modules = static_cast<int>(snapshot.modules.size());

         // Cables: "target" is the main output, "target2"... the next outputs. Each is a
         // comma-separated list of module names, or "module~control" for a modulation cable.
         size_t index = 0;
         for (const JsonValue& desktop : modules->arrayValue)
         {
            if (desktop.type != JsonValue::Type::Object || stringOr(desktop.get("type"), "").empty())
               continue;
            const ImportedModule& source = imported[index];
            const int sourceId = snapshot.modules[index].id;
            ++index;
            for (const auto& [key, value] : desktop.objectValue)
            {
               if (key.rfind("target", 0) != 0 || value.type != JsonValue::Type::String)
                  continue;
               const std::string suffix = key.substr(6);
               if (!suffix.empty() && !std::all_of(suffix.begin(), suffix.end(), [](unsigned char c)
                                                   {
                                                      return std::isdigit(c) != 0;
                                                   }))
                  continue;
               const int outputIndex = suffix.empty() ? 0 : std::max(0, std::atoi(suffix.c_str()) - 1);

               size_t start = 0;
               while (start <= value.stringValue.size())
               {
                  size_t comma = value.stringValue.find(',', start);
                  if (comma == std::string::npos)
                     comma = value.stringValue.size();
                  std::string targetName = trim(value.stringValue.substr(start, comma - start));
                  start = comma + 1;
                  if (targetName.empty())
                     continue;
                  const size_t tilde = targetName.find('~');
                  const bool controlTarget = tilde != std::string::npos;
                  if (controlTarget)
                     targetName = targetName.substr(0, tilde);
                  auto destIt = idByName.find(targetName);
                  if (destIt == idByName.end())
                  {
                     ++report.droppedTargets;
                     continue;
                  }
                  const ImportedModule& dest = imported[static_cast<size_t>(destIt->second - 1)];

                  ModuleCanvas::StateConnection connection;
                  connection.sourceModuleId = sourceId;
                  connection.destModuleId = destIt->second;
                  std::optional<PortType> sourceType;
                  if (source.adapter)
                  {
                     const auto outputs = source.adapter->outputPorts();
                     if (outputs.empty())
                     {
                        ++report.droppedTargets;
                        continue;
                     }
                     connection.sourcePortIndex = outputIndex < static_cast<int>(outputs.size()) ? outputIndex : 0;
                     sourceType = outputs[static_cast<size_t>(connection.sourcePortIndex)].type;
                  }
                  else
                  {
                     connection.sourcePortIndex = outputIndex;
                  }
                  if (dest.adapter)
                  {
                     connection.destPortIndex = compatibleInput(*dest.adapter, sourceType, controlTarget);
                     if (connection.destPortIndex < 0)
                     {
                        ++report.droppedTargets;
                        continue;
                     }
                  }
                  snapshot.connections.push_back(connection);
                  ++report.connections;
               }
            }
         }

         // Bring the patch's top-left corner into view.
         snapshot.offsetX = 40.0f - minX;
         snapshot.offsetY = 20.0f - minY;
         return true;
      }

      bool importDesktopSavestate(const uint8_t* bytes, size_t length, ModuleCanvas::StateSnapshot& snapshot,
                                  DesktopImportReport& report, std::string& error)
      {
         report = DesktopImportReport{};
         if (!bytes || length == 0)
         {
            error = "Empty file";
            return false;
         }
         std::string json;
         if (looksLikeDesktopSavestate(bytes, length))
         {
            if (!extractDesktopLayoutJson(bytes, length, json, report.saveStateRev, error))
               return false;
         }
         else
         {
            json.assign(reinterpret_cast<const char*>(bytes), length);
         }
         return importDesktopLayout(json, snapshot, report, error);
      }

   } // namespace wasm
} // namespace bespoke
