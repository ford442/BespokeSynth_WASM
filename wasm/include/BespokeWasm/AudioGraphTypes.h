/**
 * BespokeSynth WASM - Audio graph snapshot types
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/ModuleTypes.h"
#include <cstdint>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      constexpr uint32_t kAudioGraphParamAlign = 8;

      inline uint32_t alignAudioGraphOffset(uint32_t offset, uint32_t alignment = kAudioGraphParamAlign)
      {
         return (offset + (alignment - 1u)) & ~(alignment - 1u);
      }

      struct AudioGraphNode
      {
         int id = -1;
         int typeIndex = -1;
         bool enabled = true;
         uint32_t paramOffset = 0;
         uint32_t paramSize = 0;
      };

      struct AudioGraphConnection
      {
         int sourceModuleId = -1;
         int sourcePortIndex = 0;
         int destModuleId = -1;
         int destPortIndex = 0;
         PortType sourcePortType = PortType::Audio;
         PortType destPortType = PortType::Audio;
      };

      template <typename T, int Capacity>
      struct SmallVec
      {
         T data[Capacity]{};
         int count = 0;

         void clear() { count = 0; }

         void push_back(T value)
         {
            if (count < Capacity)
               data[count++] = value;
         }

         const T* begin() const { return data; }
         const T* end() const { return data + count; }
         int size() const { return count; }
      };

      struct PlanStep
      {
         int nodeIndex = -1;
         int moduleId = -1;
         int typeIndex = -1;
         WasmAudioRole audioRole = WasmAudioRole::None;
         int outBufferSlot = -1;
         SmallVec<int, 4> inputBufferSlots;
         SmallVec<int, 4> modulationSourceSlots;
         SmallVec<int, 4> noteSourceSlots;
         SmallVec<int, 4> noteSourceScratchSlots;
         bool hasNoteCable = false;
      };

      struct AudioProcessPlan
      {
         bool valid = false;
         bool hasCycle = false;
         int outputNodeIndex = -1;
         int outputBufferSlot = -1;
         int bufferSlotCount = 0;
         int modulationSlotCount = 0;
         std::vector<PlanStep> steps;
         std::vector<int> modulationSourceNodeIndices;
         std::vector<int> noteSourceNodeIndices;
         std::vector<int> nodeIndexByModuleId;
      };

      struct AudioGraphSnapshot
      {
         bool transportPlaying = false;
         float transportBPM = 120.0f;
         std::vector<AudioGraphNode> nodes;
         std::vector<AudioGraphConnection> connections;
         std::vector<uint8_t> paramArena;
         AudioProcessPlan processPlan;

         const void* paramsFor(const AudioGraphNode& node) const
         {
            if (node.paramSize == 0)
               return nullptr;
            if (static_cast<size_t>(node.paramOffset) + node.paramSize > paramArena.size())
               return nullptr;
            return paramArena.data() + node.paramOffset;
         }

         void* paramsFor(const AudioGraphNode& node)
         {
            return const_cast<void*>(static_cast<const AudioGraphSnapshot*>(this)->paramsFor(node));
         }
      };

   } // namespace wasm
} // namespace bespoke
