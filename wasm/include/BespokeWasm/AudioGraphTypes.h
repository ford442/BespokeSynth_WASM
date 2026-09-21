/**
 * BespokeSynth WASM - Audio graph snapshot types
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/ModuleTypes.h"
#include "BespokeWasm/TuningTable.h"
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
         static constexpr int kCapacity = Capacity;

         T data[Capacity]{};
         int count = 0;

         bool full() const { return count >= Capacity; }

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
         SmallVec<int, 8> inputBufferSlots;
         SmallVec<int, 8> inputBufferPorts; // dest input port index, parallel to inputBufferSlots
         SmallVec<int, 4> modulationSourceSlots;
         SmallVec<int, 4> noteSourceSlots;
         SmallVec<int, 4> noteSourceScratchSlots;
         SmallVec<int, 4> noteSourcePortFilters; // parallel; -1 = any, else WasmNoteEvent::outputPort
         SmallVec<int, 4> pulseSourceSlots;
         SmallVec<int, 4> pulseSourceScratchSlots;
         bool hasNoteCable = false;
         bool hasPulseCable = false;
      };

      struct EventSourcePlan
      {
         int nodeIndex = -1;
         SmallVec<int, 4> pulseSourceScratchSlots;
         bool hasPulseCable = false;
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
         std::vector<EventSourcePlan> noteSourcePlans;
         std::vector<int> pulseSourceNodeIndices; // upstream-first (pulse processors after their sources)
         std::vector<EventSourcePlan> pulseSourcePlans; // parallel to pulseSourceNodeIndices
         std::vector<int> nodeIndexByModuleId;
      };

      struct GraphMusicScale
      {
         // Not an aggregate (the table needs filling); keeps `{ root, mode }` initialization.
         GraphMusicScale(int rootNote = 0, int scaleMode = 6)
         : root(rootNote)
         , mode(scaleMode)
         {
            fillEqualTemperament(noteFrequency);
         }

         int root = 0;
         int mode = 6; // chromatic (identity) unless Scale UI publishes another mode
         /** true when a .scl/.kbm replaced 12-TET; mode quantization is then bypassed. */
         bool customTuning = false;
         /** MIDI note → Hz, parsed on the UI thread (12-TET unless customTuning). */
         NoteFrequencyTable noteFrequency{};
      };

      struct AudioGraphSnapshot
      {
         bool transportPlaying = false;
         float transportBPM = 120.0f;
         GraphMusicScale musicScale;
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
