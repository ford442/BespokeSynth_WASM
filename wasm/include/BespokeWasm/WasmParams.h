/**
 * BespokeSynth WASM - Adapter parameter tables (ADR: docs/wasm/module-porting.md)
 *
 * Every adapter declares its controls once, as a constexpr ParamDescriptor table. The
 * table is the serialized control list (patch JSON, MIDI learn, scripts address controls
 * by name) and the audio parameter layout: a descriptor with an offset says where and how
 * the value lands in the adapter's POD Params block. ParamId is the index into the table,
 * so a control change reaches the engine as { moduleId, paramId, value } without a
 * snapshot rebuild (ParamChange, AudioGraphEngine::queueParamChange).
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace bespoke
{
   namespace wasm
   {

      using ParamId = uint16_t;

      /** How a control's float value is stored in the Params block. */
      enum class ParamKind : uint8_t
      {
         Float, // float as is
         Int, // int, truncated toward zero
         IntRound, // int, rounded to nearest
         Bool, // bool, value >= 0.5
         Choice1, // bool, rounded value == 1 (two-way choice controls)
         Mask16, // int, truncated and masked to 16 bits (step patterns)
      };

      enum ParamFlags : uint8_t
      {
         kParamNoFlags = 0,
         /** Clamp to [minValue, maxValue] before conversion. */
         kParamClamp = 1 << 0,
      };

      /** Offset of a serialized control the audio side never reads (transport bpm, scale root). */
      constexpr uint32_t kUiOnlyParam = 0xFFFFFFFFu;

      struct ParamDescriptor
      {
         ParamId id = 0;
         const char* name = "";
         float minValue = 0.0f;
         float maxValue = 1.0f;
         float defaultValue = 0.0f;
         ParamKind kind = ParamKind::Float;
         uint32_t offset = kUiOnlyParam;
         /** One-pole glide for queued changes (0 = jump). Float params only. */
         float smoothingMs = 0.0f;
         uint8_t flags = kParamNoFlags;

         constexpr bool isAudio() const { return offset != kUiOnlyParam; }
      };

      using ParamTable = std::span<const ParamDescriptor>;

      /** Glide time for level-like controls (gain, mix, pan, cutoff). */
      constexpr float kParamSmoothingMs = 20.0f;

      /** The table's ids must equal their index; checked by test_param_tables. */
      constexpr bool paramTableIdsAreIndices(ParamTable table)
      {
         for (size_t i = 0; i < table.size(); ++i)
         {
            if (table[i].id != i)
               return false;
         }
         return true;
      }

      [[nodiscard]] constexpr const ParamDescriptor* findParam(ParamTable table, std::string_view name)
      {
         for (const auto& param : table)
         {
            if (name == param.name)
               return &param;
         }
         return nullptr;
      }

      /** Converts `value` per the descriptor and stores it in `params` (no-op for UI-only ones). */
      void writeParam(void* params, const ParamDescriptor& param, float value);
      /** The stored value as a float (what writeParam would compare against). */
      [[nodiscard]] float readParam(const void* params, const ParamDescriptor& param);
      /** True when writeParam(value) would leave the stored bytes unchanged. */
      [[nodiscard]] bool paramMatches(const void* params, const ParamDescriptor& param, float value);

      /** A control change for the audio side. moduleId < 0 addresses a GlobalParam. */
      struct ParamChange
      {
         int32_t moduleId = -1;
         ParamId paramId = 0;
         float value = 0.0f;
         /** Publish order (ModuleCanvas's counter); a snapshot carries the last seq it reflects. */
         uint32_t seq = 0;
      };

      /** Snapshot-wide values that also travel as ParamChange (moduleId kGlobalParamModule). */
      enum class GlobalParam : ParamId
      {
         TransportPlaying = 0,
         TransportBpm = 1,
      };

      constexpr int32_t kGlobalParamModule = -1;

   } // namespace wasm
} // namespace bespoke
