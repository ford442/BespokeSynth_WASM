/**
 * BespokeSynth WASM - Patch / module / sample C ABI
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmBridge.h"
#include "BespokeWasm/WasmBridgeInternal.h"
#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/WasmPatchState.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/WasmRuntimeState.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <emscripten.h>

using namespace bespoke::wasm;

namespace
{
   std::string gLastSampleHash;
   std::string gLastSampleName;
   std::string gModuleStringBuffer;
   std::string gSampleError;
}

static char* jsonEscape(char* dst, const char* end, const char* src)
{
   for (; *src && dst < end - 1; ++src)
   {
      if (*src == '"' || *src == '\\')
      {
         if (dst < end - 2)
            *dst++ = '\\';
         else
            break;
      }
      *dst++ = *src;
   }
   return dst;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE int bespoke_create_module(const char* type, float x, float y)
{
   auto& state = runtimeState();
   if (state.canvas)
   {
      int id = state.canvas->createModule(type, x, y);
      printf("BespokeSynth WASM: Created module '%s' (id=%d) at (%.1f, %.1f)\n", type, id, x, y);
      return id;
   }
   printf("BespokeSynth WASM: Cannot create module - canvas not initialized\n");
   return -1;
}

EMSCRIPTEN_KEEPALIVE void bespoke_delete_module(int moduleId)
{
   if (runtimeState().canvas)
      runtimeState().canvas->deleteModule(moduleId);
   printf("BespokeSynth WASM: Delete module %d\n", moduleId);
}

EMSCRIPTEN_KEEPALIVE void bespoke_connect_modules(int sourceId, int destId)
{
   if (runtimeState().canvas)
      runtimeState().canvas->connectModules(sourceId, 0, destId, 0);
   printf("BespokeSynth WASM: Connect %d -> %d\n", sourceId, destId);
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_view_mode(void)
{
   return runtimeState().viewMode;
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_view_mode(int mode)
{
   auto& state = runtimeState();
   if (!state.legacyPanelsEnabled)
   {
      state.viewMode = 0;
      return;
   }
   state.viewMode = (mode == 0) ? 0 : 1;
   printf("BespokeSynth WASM: Set view mode to %d (%s)\n",
          state.viewMode, state.viewMode == 0 ? "Modular Canvas" : "Demo Panels");
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_legacy_panels(int enabled)
{
   auto& state = runtimeState();
   state.legacyPanelsEnabled = enabled != 0;
   if (state.legacyPanelsEnabled)
      ensureLegacyDemoKnobs();
   else
   {
      state.viewMode = 0;
      state.knobs.clear();
      state.controlInfoCache.clear();
   }
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_legacy_panels(void)
{
   return runtimeState().legacyPanelsEnabled ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void bespoke_set_control_value(int moduleId, const char* controlName, float value)
{
   auto& state = runtimeState();
   if (state.canvas)
      state.canvas->setModuleControlValue(moduleId, controlName ? controlName : "", value);
}

EMSCRIPTEN_KEEPALIVE float bespoke_get_control_value(int moduleId, const char* controlName)
{
   auto& state = runtimeState();
   if (state.canvas)
   {
      float value = 0.0f;
      if (state.canvas->getModuleControlValue(moduleId, controlName ? controlName : "", value))
         return value;
   }
   return 0.0f;
}

EMSCRIPTEN_KEEPALIVE int bespoke_load_state_json(const char* json);

EMSCRIPTEN_KEEPALIVE int bespoke_save_state(const char* filename)
{
   auto& state = runtimeState();
   if (!state.canvas || !filename || filename[0] == '\0')
      return -1;

   const std::string json = serializePatchState(state.canvas->createStateSnapshot(), state.viewMode);
   std::ofstream out(filename, std::ios::binary);
   if (!out)
   {
      printf("BespokeSynth WASM: Failed to open state file '%s' for writing\n", filename);
      return -2;
   }
   out << json;
   printf("BespokeSynth WASM: Saved state to '%s' (%zu bytes)\n", filename, json.size());
   return out.good() ? 0 : -3;
}

EMSCRIPTEN_KEEPALIVE int bespoke_load_state(const char* filename)
{
   if (!runtimeState().canvas || !filename || filename[0] == '\0')
      return -1;

   std::ifstream in(filename, std::ios::binary);
   if (!in)
   {
      printf("BespokeSynth WASM: Failed to open state file '%s' for reading\n", filename);
      return -2;
   }

   std::ostringstream contents;
   contents << in.rdbuf();
   return bespoke_load_state_json(contents.str().c_str());
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_state_json(void)
{
   auto& state = runtimeState();
   if (!state.canvas)
   {
      state.stateJsonBuffer = "{}";
      return state.stateJsonBuffer.c_str();
   }

   state.stateJsonBuffer = serializePatchState(state.canvas->createStateSnapshot(), state.viewMode);
   return state.stateJsonBuffer.c_str();
}

EMSCRIPTEN_KEEPALIVE int bespoke_load_state_json(const char* json)
{
   auto& state = runtimeState();
   if (!state.canvas || !json)
      return -1;

   ModuleCanvas::StateSnapshot snapshot;
   int loadedViewMode = state.viewMode;
   state.stateErrorMessage.clear();
   if (!deserializePatchState(json, snapshot, loadedViewMode, state.stateErrorMessage))
   {
      printf("BespokeSynth WASM: Failed to load state JSON: %s\n", state.stateErrorMessage.c_str());
      return -2;
   }

   if (state.legacyPanelsEnabled)
      state.viewMode = (loadedViewMode == 0) ? 0 : 1;
   else
      state.viewMode = 0;

   if (!state.canvas->applyStateSnapshot(snapshot))
      return -3;

   state.oscillatorModuleId = state.canvas->findFirstModuleOfType("oscillator");
   state.gainModuleId = state.canvas->findFirstModuleOfType("gain");
   state.audioGraphEngine = std::make_unique<AudioGraphEngine>();
   printf("BespokeSynth WASM: Loaded state JSON (%zu modules, %zu connections)\n",
          snapshot.modules.size(), snapshot.connections.size());
   return 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_load_layout(const char* path)
{
   if (!path || path[0] == '\0')
      return -1;

   std::string resourcePath(path);
   if (resourcePath.find("..") != std::string::npos)
      return -1;
   if (resourcePath.rfind("/resource/", 0) != 0)
      resourcePath = "/resource/userdata_original/" + resourcePath;

   std::ifstream in(resourcePath, std::ios::binary);
   if (!in)
   {
      printf("BespokeSynth WASM: Bundled layout not found: '%s'\n", resourcePath.c_str());
      return -2;
   }

   std::ostringstream contents;
   contents << in.rdbuf();
   const int result = bespoke_load_state_json(contents.str().c_str());
   if (result != 0)
      printf("BespokeSynth WASM: '%s' is not a WASM patch-state layout (result=%d)\n",
             resourcePath.c_str(), result);
   return result;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_module_count(void)
{
   if (runtimeState().canvas)
      return runtimeState().canvas->getModuleCount();
   return 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_get_control_count(void)
{
   return static_cast<int>(runtimeState().controlInfoCache.size());
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_control_info(int index)
{
   static char sJsonBuf[512];
   auto& cache = runtimeState().controlInfoCache;

   if (index < 0 || index >= static_cast<int>(cache.size()))
   {
      snprintf(sJsonBuf, sizeof(sJsonBuf), "{}");
      return sJsonBuf;
   }

   const ControlInfoEntry& e = cache[index];
   char* p = sJsonBuf;
   char* end = sJsonBuf + sizeof(sJsonBuf) - 1;

   p += snprintf(p, end - p, "{\"id\":%d,\"type\":\"%s\",\"label\":\"", e.id, e.type);
   p = jsonEscape(p, end, e.label);
   p += snprintf(p, end - p, "\",\"value\":%.6f,\"min\":%.6f,\"max\":%.6f,\"unit\":\"",
                 e.value, e.min, e.max);
   p = jsonEscape(p, end, e.unit);
   p += snprintf(p, end - p, "\",\"x\":%.2f,\"y\":%.2f,\"size\":%.2f}",
                 e.x, e.y, e.size);
   *p = '\0';
   return sJsonBuf;
}

EMSCRIPTEN_KEEPALIVE int bespoke_load_sample(const unsigned char* bytes, int len, const char* name)
{
   auto& state = runtimeState();
   int deviceRate = 44100;
   if (state.audioBackend)
      deviceRate = state.audioBackend->getSampleRate();
   else if (state.externalSampleRate > 0.0f)
      deviceRate = static_cast<int>(state.externalSampleRate);

   const int id = SampleStore::instance().loadFromMemory(bytes, len, name ? name : "sample", deviceRate);
   if (id < 0)
   {
      gSampleError = "Failed to decode sample";
      gLastSampleHash.clear();
      gLastSampleName.clear();
      return -1;
   }
   if (const SampleBuffer* sample = SampleStore::instance().findById(id))
   {
      gLastSampleHash = sample->hash();
      gLastSampleName = sample->name();
   }
   return id;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_sample_hash(int sampleId)
{
   if (sampleId < 0)
      return gLastSampleHash.c_str();
   if (const SampleBuffer* sample = SampleStore::instance().findById(sampleId))
   {
      gLastSampleHash = sample->hash();
      return gLastSampleHash.c_str();
   }
   return "";
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_sample_name(int sampleId)
{
   if (const SampleBuffer* sample = SampleStore::instance().findById(sampleId))
   {
      gLastSampleName = sample->name();
      return gLastSampleName.c_str();
   }
   return "";
}

EMSCRIPTEN_KEEPALIVE int bespoke_assign_sample(int moduleId, const char* hash)
{
   auto& state = runtimeState();
   if (!state.canvas || !hash)
      return 0;
   return state.canvas->setModuleStringProperty(moduleId, "sampleHash", hash) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bespoke_find_first_module(const char* type)
{
   auto& state = runtimeState();
   if (!state.canvas || !type)
      return -1;
   return state.canvas->findFirstModuleOfType(type);
}

EMSCRIPTEN_KEEPALIVE int bespoke_set_module_string(int moduleId, const char* key, const char* value)
{
   auto& state = runtimeState();
   if (!state.canvas || !key)
      return 0;
   return state.canvas->setModuleStringProperty(moduleId, key, value ? value : "") ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* bespoke_get_module_string(int moduleId, const char* key)
{
   gModuleStringBuffer.clear();
   auto& state = runtimeState();
   if (!state.canvas || !key)
      return "";
   if (!state.canvas->getModuleStringProperty(moduleId, key, gModuleStringBuffer))
      return "";
   return gModuleStringBuffer.c_str();
}

} // extern "C"
