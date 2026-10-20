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
#include <span>
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

      /** A run of entries in one of AudioProcessPlan's flat input lists. */
      struct PlanRange
      {
         uint32_t begin = 0;
         uint32_t count = 0;

         bool empty() const { return count == 0; }
      };

      /** How the engine adapts a cable's width to the destination port (PortChannels). */
      enum class PlanChannelMix : uint8_t
      {
         AsIs = 0,
         ToMono = 1, // stereo cable into a mono port: (L + R) / 2
         ToStereo = 2, // mono cable into a stereo port: L on both sides
      };

      /**
       * One module in topological order. Input lists are ranges into the plan's flat vectors
       * (sized when the plan compiles), so a module takes any number of cables.
       */
      struct PlanStep
      {
         int nodeIndex = -1;
         int moduleId = -1;
         int typeIndex = -1;
         WasmAudioRole audioRole = WasmAudioRole::None;
         int outBufferSlot = -1; // the first Audio output
         PlanRange auxOutputs; // auxOutputSlots: the 2nd, 3rd... Audio outputs (send)
         PlanRange audioInputs; // audioInputSlots / audioInputPorts / audioInputMix
         PlanRange modulationSources; // modulationSourceSlots
         PlanRange noteSources; // noteScratchSlots / notePortFilters
         PlanRange pulseSources; // pulseScratchSlots
         bool hasNoteCable = false;
         bool hasPulseCable = false;
      };

      struct EventSourcePlan
      {
         int nodeIndex = -1;
         PlanRange pulseSources; // upstream pulse emitters (earlier slots)
         bool hasPulseCable = false;
         // Note processors: upstream emitters, always earlier slots of the same plan.
         PlanRange noteSources;
         bool hasNoteCable = false;
         bool isProcessor = false;
         /**
          * Notes go to the selected MIDI output. False for an emitter whose every Note cable
          * ends in a processor: the processor's output is what the patch plays.
          */
         bool reportsToMidiOut = true;
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
         /** Parallel to modulationSourceNodeIndices: the Note / Pulse cables into each (envelope, ramper). */
         std::vector<EventSourcePlan> modulationSourcePlans;
         // Note emitters (sources and processors), upstream-first so a processor runs after its feeders.
         std::vector<int> noteSourceNodeIndices;
         std::vector<EventSourcePlan> noteSourcePlans; // parallel to noteSourceNodeIndices
         /** A note processor with no Note cable listens to the performance (QWERTY / MIDI in). */
         bool performanceNotesProcessed = false;
         std::vector<int> pulseSourceNodeIndices; // upstream-first (pulse processors after their sources)
         std::vector<EventSourcePlan> pulseSourcePlans; // parallel to pulseSourceNodeIndices
         std::vector<int> nodeIndexByModuleId;

         // Flat input lists that PlanStep / EventSourcePlan ranges index.
         std::vector<int> audioInputSlots; // source audio buffer slot
         std::vector<int> audioInputPorts; // destination input port, parallel
         std::vector<PlanChannelMix> audioInputMix; // parallel
         std::vector<int> modulationSourceSlots;
         std::vector<int> noteScratchSlots; // note emitter scratch slot
         std::vector<int> notePortFilters; // parallel; -1 = any, else WasmNoteEvent::outputPort
         std::vector<int> pulseScratchSlots;
         std::vector<int> auxOutputSlots; // audio buffer slot per extra Audio output, in port order
         /** Largest audio fan-in of any step (sizes the engine's per-step input scratch). */
         int maxAudioFanIn = 0;

         std::span<const int> slice(const std::vector<int>& list, PlanRange range) const
         {
            if (range.begin > list.size() || range.count > list.size() - range.begin)
               return {};
            return { list.data() + range.begin, range.count };
         }
         std::span<const int> audioInputSlotsOf(const PlanStep& step) const { return slice(audioInputSlots, step.audioInputs); }
         std::span<const int> audioInputPortsOf(const PlanStep& step) const { return slice(audioInputPorts, step.audioInputs); }
         std::span<const int> modulationSlotsOf(const PlanStep& step) const
         {
            return slice(modulationSourceSlots, step.modulationSources);
         }
         std::span<const int> noteScratchSlotsOf(PlanRange range) const { return slice(noteScratchSlots, range); }
         std::span<const int> notePortFiltersOf(PlanRange range) const { return slice(notePortFilters, range); }
         std::span<const int> pulseScratchSlotsOf(PlanRange range) const { return slice(pulseScratchSlots, range); }
         std::span<const int> auxOutputSlotsOf(const PlanStep& step) const { return slice(auxOutputSlots, step.auxOutputs); }
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
         /**
          * Topology publish counter (ModuleCanvas). The engine re-reads the param arena only when it
          * changes; 0 marks an untracked snapshot (tests, offline), read afresh every block.
          */
         uint64_t generation = 0;
         /** Last ParamChange::seq reflected in paramArena; later queued changes still apply. */
         uint32_t paramSeq = 0;
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
