/**
 * BespokeSynth WASM - Adapter parameter tables
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmParams.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         struct EncodedParam
         {
            alignas(8) uint8_t bytes[8]{};
            size_t size = 0;

            template <typename T>
            void set(T value)
            {
               std::memcpy(bytes, &value, sizeof(T));
               size = sizeof(T);
            }
         };

         /** The bytes writeParam stores for `value` (clamped and converted per the descriptor). */
         EncodedParam encode(const ParamDescriptor& param, float value)
         {
            if ((param.flags & kParamClamp) != 0)
               value = std::max(param.minValue, std::min(param.maxValue, value));
            EncodedParam encoded;
            switch (param.kind)
            {
               case ParamKind::Float:
                  encoded.set(value);
                  break;
               case ParamKind::Int:
                  encoded.set(static_cast<int>(value));
                  break;
               case ParamKind::IntRound:
                  encoded.set(static_cast<int>(std::lround(value)));
                  break;
               case ParamKind::Bool:
                  encoded.set(value >= 0.5f);
                  break;
               case ParamKind::Choice1:
                  encoded.set(std::lround(value) == 1);
                  break;
               case ParamKind::Mask16:
                  encoded.set(static_cast<int>(value) & 0xFFFF);
                  break;
            }
            return encoded;
         }

         // Params blocks are only aligned to kAudioGraphParamAlign; memcpy keeps every access defined.
         template <typename T>
         T load(const void* params, uint32_t offset)
         {
            T value{};
            std::memcpy(&value, static_cast<const uint8_t*>(params) + offset, sizeof(T));
            return value;
         }
      }

      void writeParam(void* params, const ParamDescriptor& param, float value)
      {
         if (!params || !param.isAudio())
            return;
         const EncodedParam encoded = encode(param, value);
         std::memcpy(static_cast<uint8_t*>(params) + param.offset, encoded.bytes, encoded.size);
      }

      float readParam(const void* params, const ParamDescriptor& param)
      {
         if (!params || !param.isAudio())
            return 0.0f;
         switch (param.kind)
         {
            case ParamKind::Float:
               return load<float>(params, param.offset);
            case ParamKind::Int:
            case ParamKind::IntRound:
            case ParamKind::Mask16:
               return static_cast<float>(load<int>(params, param.offset));
            case ParamKind::Bool:
            case ParamKind::Choice1:
               return load<bool>(params, param.offset) ? 1.0f : 0.0f;
         }
         return 0.0f;
      }

      bool paramMatches(const void* params, const ParamDescriptor& param, float value)
      {
         if (!params || !param.isAudio())
            return true;
         // Stored representations, so 0.4 and 0.0 match for an Int control.
         const EncodedParam encoded = encode(param, value);
         return std::memcmp(encoded.bytes, static_cast<const uint8_t*>(params) + param.offset, encoded.size) == 0;
      }

   } // namespace wasm
} // namespace bespoke
