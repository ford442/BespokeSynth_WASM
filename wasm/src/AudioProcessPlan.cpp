/**
 * BespokeSynth WASM - Audio process plan compiler
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioProcessPlan.h"
#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/WasmModuleAdapter.h"

#include <algorithm>
#include <atomic>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         std::atomic<uint64_t> gPlanCompileCount{ 0 };

         /** Range over the entries appended to `list` since `begin`. */
         PlanRange rangeSince(const std::vector<int>& list, size_t begin)
         {
            return { static_cast<uint32_t>(begin), static_cast<uint32_t>(list.size() - begin) };
         }

         PlanChannelMix channelMixFor(const WasmModuleAdapter* adapter, int destPort)
         {
            if (!adapter)
               return PlanChannelMix::AsIs;
            const auto inputs = adapter->inputPorts();
            if (destPort < 0 || destPort >= static_cast<int>(inputs.size()))
               return PlanChannelMix::AsIs;
            switch (inputs[static_cast<size_t>(destPort)].channels)
            {
               case PortChannels::Mono:
                  return PlanChannelMix::ToMono;
               case PortChannels::Stereo:
                  return PlanChannelMix::ToStereo;
               default:
                  return PlanChannelMix::AsIs;
            }
         }

         bool participatesInAudioTopology(const WasmModuleAdapter* adapter)
         {
            if (!adapter)
               return false;
            switch (adapter->audioRole())
            {
               case WasmAudioRole::AudioSource:
               case WasmAudioRole::AudioProcessor:
               case WasmAudioRole::Sink:
                  return true;
               default:
                  return false;
            }
         }

         /** Ordinal of output `portIndex` among the adapter's Audio outputs (0 for a legacy/unknown port). */
         int audioOutputOrdinal(const WasmModuleAdapter* adapter, int portIndex)
         {
            if (!adapter)
               return 0;
            const auto outputs = adapter->outputPorts();
            int ordinal = 0;
            for (int i = 0; i < static_cast<int>(outputs.size()); ++i)
            {
               if (outputs[static_cast<size_t>(i)].type != PortType::Audio)
                  continue;
               if (i == portIndex)
                  return ordinal;
               ++ordinal;
            }
            return 0;
         }

         int audioOutputCount(const WasmModuleAdapter* adapter)
         {
            if (!adapter)
               return 1;
            int count = 0;
            for (const PortDescriptor& port : adapter->outputPorts())
               count += port.type == PortType::Audio ? 1 : 0;
            return std::max(1, count);
         }

         /** One audio output of one module (the key of its buffer slot). */
         int64_t audioOutputKey(int moduleId, int ordinal)
         {
            return (static_cast<int64_t>(moduleId) << 8) | static_cast<int64_t>(ordinal & 0xff);
         }

         /** -1 when the source has at most one Note output (no filtering), else the Note ordinal. */
         int noteOutputOrdinal(const WasmModuleAdapter* adapter, int sourcePortIndex)
         {
            if (!adapter)
               return -1;
            int noteOutputs = 0;
            int ordinal = -1;
            const auto outputs = adapter->outputPorts();
            for (int i = 0; i < static_cast<int>(outputs.size()); ++i)
            {
               if (outputs[static_cast<size_t>(i)].type != PortType::Note)
                  continue;
               if (i == sourcePortIndex)
                  ordinal = noteOutputs;
               ++noteOutputs;
            }
            return noteOutputs > 1 ? ordinal : -1;
         }

         /**
          * Order event emitters so processors (clockdivider for pulses; chorder, arp, filter,
          * router for notes) run after the emitters feeding them. Nodes caught in a cycle keep
          * their original relative order; the edge that closes the cycle is ignored by the engine
          * (it only reads earlier scratch slots).
          */
         std::vector<int> orderEventSources(const AudioGraphSnapshot& snapshot,
                                            const std::vector<int>& nodeIndices,
                                            const std::unordered_map<int, std::vector<std::pair<int, int>>>& inputsByDest)
         {
            std::unordered_map<int, int> positionByModuleId;
            for (int i = 0; i < static_cast<int>(nodeIndices.size()); ++i)
               positionByModuleId[snapshot.nodes[static_cast<size_t>(nodeIndices[static_cast<size_t>(i)])].id] = i;

            std::vector<int> indegree(nodeIndices.size(), 0);
            std::vector<std::vector<int>> children(nodeIndices.size());
            for (int i = 0; i < static_cast<int>(nodeIndices.size()); ++i)
            {
               const int moduleId = snapshot.nodes[static_cast<size_t>(nodeIndices[static_cast<size_t>(i)])].id;
               auto inputs = inputsByDest.find(moduleId);
               if (inputs == inputsByDest.end())
                  continue;
               for (const auto& [sourceId, sourcePort] : inputs->second)
               {
                  (void)sourcePort;
                  auto src = positionByModuleId.find(sourceId);
                  if (src == positionByModuleId.end() || src->second == i)
                     continue;
                  children[static_cast<size_t>(src->second)].push_back(i);
                  ++indegree[static_cast<size_t>(i)];
               }
            }

            std::vector<int> ordered;
            std::vector<bool> placed(nodeIndices.size(), false);
            ordered.reserve(nodeIndices.size());
            bool progress = true;
            while (progress)
            {
               progress = false;
               for (int i = 0; i < static_cast<int>(nodeIndices.size()); ++i)
               {
                  if (placed[static_cast<size_t>(i)] || indegree[static_cast<size_t>(i)] != 0)
                     continue;
                  placed[static_cast<size_t>(i)] = true;
                  ordered.push_back(nodeIndices[static_cast<size_t>(i)]);
                  for (int child : children[static_cast<size_t>(i)])
                     --indegree[static_cast<size_t>(child)];
                  progress = true;
               }
            }
            for (int i = 0; i < static_cast<int>(nodeIndices.size()); ++i)
            {
               if (!placed[static_cast<size_t>(i)])
                  ordered.push_back(nodeIndices[static_cast<size_t>(i)]);
            }
            return ordered;
         }
      }

      uint64_t audioPlanCompileCount()
      {
         return gPlanCompileCount.load(std::memory_order_relaxed);
      }

      void compileAudioProcessPlan(const AudioGraphSnapshot& snapshot, AudioProcessPlan& plan)
      {
         gPlanCompileCount.fetch_add(1, std::memory_order_relaxed);
         plan = AudioProcessPlan{};
         if (snapshot.nodes.empty())
            return;

         auto& registry = WasmModuleAdapterRegistry::instance();

         int maxModuleId = -1;
         for (const auto& node : snapshot.nodes)
            maxModuleId = std::max(maxModuleId, node.id);

         plan.nodeIndexByModuleId.assign(static_cast<size_t>(maxModuleId + 1), -1);
         for (size_t i = 0; i < snapshot.nodes.size(); ++i)
         {
            const int id = snapshot.nodes[i].id;
            if (id >= 0 && id < static_cast<int>(plan.nodeIndexByModuleId.size()))
               plan.nodeIndexByModuleId[static_cast<size_t>(id)] = static_cast<int>(i);
         }

         struct AudioEdge
         {
            int sourceId = -1;
            int sourceOutput = 0; // ordinal among the source's Audio outputs
            int destPort = 0;
         };
         // (source module id, port) pairs: source output port for note/pulse.
         std::unordered_map<int, std::vector<AudioEdge>> audioInputsByDest;
         std::unordered_map<int64_t, int> cablesByAudioOutput;
         std::unordered_map<int, std::vector<int>> modulationInputsByDest;
         std::unordered_map<int, std::vector<std::pair<int, int>>> noteInputsByDest;
         std::unordered_map<int, std::vector<std::pair<int, int>>> pulseInputsByDest;
         std::unordered_map<int, std::vector<int>> audioChildrenBySource;
         std::unordered_map<int, std::vector<int>> noteDestsBySource;
         std::unordered_map<int, int> indegree;
         std::unordered_set<int> audioModuleIds;

         for (size_t nodeIndex = 0; nodeIndex < snapshot.nodes.size(); ++nodeIndex)
         {
            const auto& node = snapshot.nodes[nodeIndex];
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            if (!node.enabled || !adapter)
               continue;

            if (participatesInAudioTopology(adapter))
            {
               audioModuleIds.insert(node.id);
               indegree[node.id] = 0;
            }

            if (adapter->audioRole() == WasmAudioRole::ModulationSource)
               plan.modulationSourceNodeIndices.push_back(static_cast<int>(nodeIndex));
            if (adapter->audioRole() == WasmAudioRole::NoteSource ||
                adapter->audioRole() == WasmAudioRole::NoteProcessor)
               plan.noteSourceNodeIndices.push_back(static_cast<int>(nodeIndex));
            if (adapter->audioRole() == WasmAudioRole::PulseSource)
               plan.pulseSourceNodeIndices.push_back(static_cast<int>(nodeIndex));
         }

         for (const auto& conn : snapshot.connections)
         {
            if (conn.sourcePortType == PortType::Audio && conn.destPortType == PortType::Audio &&
                audioModuleIds.count(conn.sourceModuleId) && audioModuleIds.count(conn.destModuleId))
            {
               const int sourceNode = conn.sourceModuleId < static_cast<int>(plan.nodeIndexByModuleId.size())
                                      ? plan.nodeIndexByModuleId[static_cast<size_t>(conn.sourceModuleId)]
                                      : -1;
               const WasmModuleAdapter* sourceAdapter =
               sourceNode >= 0 ? registry.adapterAt(snapshot.nodes[static_cast<size_t>(sourceNode)].typeIndex) : nullptr;
               const int sourceOutput = audioOutputOrdinal(sourceAdapter, conn.sourcePortIndex);
               audioInputsByDest[conn.destModuleId].push_back({ conn.sourceModuleId, sourceOutput, conn.destPortIndex });
               audioChildrenBySource[conn.sourceModuleId].push_back(conn.destModuleId);
               ++cablesByAudioOutput[audioOutputKey(conn.sourceModuleId, sourceOutput)];
               ++indegree[conn.destModuleId];
            }
            else if (conn.sourcePortType == PortType::Modulation &&
                     conn.destPortType == PortType::Modulation)
            {
               modulationInputsByDest[conn.destModuleId].push_back(conn.sourceModuleId);
            }
            else if (conn.sourcePortType == PortType::Note && conn.destPortType == PortType::Note)
            {
               noteInputsByDest[conn.destModuleId].push_back({ conn.sourceModuleId, conn.sourcePortIndex });
               noteDestsBySource[conn.sourceModuleId].push_back(conn.destModuleId);
            }
            else if (conn.sourcePortType == PortType::Pulse && conn.destPortType == PortType::Pulse)
            {
               pulseInputsByDest[conn.destModuleId].push_back({ conn.sourceModuleId, conn.sourcePortIndex });
            }
         }

         std::queue<int> ready;
         for (const auto& [id, degree] : indegree)
         {
            if (degree == 0)
               ready.push(id);
         }

         std::vector<int> topoModuleIds;
         while (!ready.empty())
         {
            const int id = ready.front();
            ready.pop();
            topoModuleIds.push_back(id);

            for (int child : audioChildrenBySource[id])
            {
               auto it = indegree.find(child);
               if (it != indegree.end() && --it->second == 0)
                  ready.push(child);
            }
         }

         if (topoModuleIds.size() != audioModuleIds.size())
         {
            plan.hasCycle = true;
            return;
         }

         std::unordered_map<int64_t, int> audioSlotByOutput; // audioOutputKey -> buffer slot
         std::unordered_map<int, int> slotRefCount;
         std::vector<int> freeSlots;
         int nextSlot = 0;

         auto acquireSlot = [&](const PlanStep& step) -> int
         {
            for (auto it = freeSlots.begin(); it != freeSlots.end(); ++it)
            {
               bool usedAsInput = false;
               for (int inputSlot : plan.audioInputSlotsOf(step))
               {
                  if (inputSlot == *it)
                  {
                     usedAsInput = true;
                     break;
                  }
               }
               if (!usedAsInput)
               {
                  const int slot = *it;
                  freeSlots.erase(it);
                  return slot;
               }
            }
            return nextSlot++;
         };

         auto releaseSlot = [&](int slot)
         {
            if (slot < 0)
               return;
            auto it = slotRefCount.find(slot);
            if (it == slotRefCount.end())
               return;
            if (--it->second == 0)
            {
               slotRefCount.erase(it);
               freeSlots.push_back(slot);
            }
         };

         std::unordered_map<int, int> modulationSlotByModuleId;
         int nextModSlot = 0;
         for (int lfoNodeIndex : plan.modulationSourceNodeIndices)
            modulationSlotByModuleId[snapshot.nodes[static_cast<size_t>(lfoNodeIndex)].id] = nextModSlot++;

         plan.modulationSlotCount = nextModSlot;

         plan.pulseSourceNodeIndices = orderEventSources(snapshot, plan.pulseSourceNodeIndices, pulseInputsByDest);

         std::unordered_map<int, int> pulseScratchByModuleId;
         for (int scratch = 0; scratch < static_cast<int>(plan.pulseSourceNodeIndices.size()); ++scratch)
         {
            const int nodeIndex = plan.pulseSourceNodeIndices[static_cast<size_t>(scratch)];
            pulseScratchByModuleId[snapshot.nodes[static_cast<size_t>(nodeIndex)].id] = scratch;
         }

         // Appends the pulse scratch slots feeding `destModuleId` (below `beforeSlot` when >= 0).
         auto fillPulseScratchSlots = [&](PlanRange& range, bool& hasPulseCable, int destModuleId, int beforeSlot)
         {
            const size_t begin = plan.pulseScratchSlots.size();
            const auto pulseInputs = pulseInputsByDest.find(destModuleId);
            if (pulseInputs != pulseInputsByDest.end())
            {
               hasPulseCable = true;
               for (const auto& [sourceId, sourcePort] : pulseInputs->second)
               {
                  (void)sourcePort;
                  auto it = pulseScratchByModuleId.find(sourceId);
                  if (it != pulseScratchByModuleId.end() && (beforeSlot < 0 || it->second < beforeSlot))
                     plan.pulseScratchSlots.push_back(it->second);
               }
            }
            range = rangeSince(plan.pulseScratchSlots, begin);
         };

         plan.pulseSourcePlans.resize(plan.pulseSourceNodeIndices.size());
         for (int scratch = 0; scratch < static_cast<int>(plan.pulseSourceNodeIndices.size()); ++scratch)
         {
            EventSourcePlan& sourcePlan = plan.pulseSourcePlans[static_cast<size_t>(scratch)];
            sourcePlan.nodeIndex = plan.pulseSourceNodeIndices[static_cast<size_t>(scratch)];
            const int destId = snapshot.nodes[static_cast<size_t>(sourcePlan.nodeIndex)].id;
            // Only sources evaluated earlier this block can feed this one (cycles are cut here).
            fillPulseScratchSlots(sourcePlan.pulseSources, sourcePlan.hasPulseCable, destId, scratch);
         }

         // Note emitters, upstream-first: a processor reads only earlier scratch slots.
         plan.noteSourceNodeIndices = orderEventSources(snapshot, plan.noteSourceNodeIndices, noteInputsByDest);
         std::unordered_map<int, int> noteScratchByModuleId;
         std::unordered_set<int> noteProcessorIds;
         for (int slot = 0; slot < static_cast<int>(plan.noteSourceNodeIndices.size()); ++slot)
         {
            const auto& node = snapshot.nodes[static_cast<size_t>(plan.noteSourceNodeIndices[static_cast<size_t>(slot)])];
            noteScratchByModuleId[node.id] = slot;
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            if (adapter && adapter->audioRole() == WasmAudioRole::NoteProcessor)
               noteProcessorIds.insert(node.id);
         }

         plan.noteSourcePlans.resize(plan.noteSourceNodeIndices.size());
         for (int slot = 0; slot < static_cast<int>(plan.noteSourceNodeIndices.size()); ++slot)
         {
            EventSourcePlan& sourcePlan = plan.noteSourcePlans[static_cast<size_t>(slot)];
            sourcePlan.nodeIndex = plan.noteSourceNodeIndices[static_cast<size_t>(slot)];
            const auto& node = snapshot.nodes[static_cast<size_t>(sourcePlan.nodeIndex)];
            sourcePlan.isProcessor = noteProcessorIds.count(node.id) != 0;

            if (sourcePlan.isProcessor)
            {
               const auto noteInputs = noteInputsByDest.find(node.id);
               if (noteInputs == noteInputsByDest.end())
               {
                  plan.performanceNotesProcessed = true;
               }
               else
               {
                  sourcePlan.hasNoteCable = true;
                  const size_t begin = plan.noteScratchSlots.size();
                  for (const auto& [sourceId, sourcePort] : noteInputs->second)
                  {
                     auto scratch = noteScratchByModuleId.find(sourceId);
                     // Cycle edges (a later or equal slot) are dropped, like pulse cycles.
                     if (scratch == noteScratchByModuleId.end() || scratch->second >= slot)
                        continue;
                     const auto& sourceNode = snapshot.nodes[static_cast<size_t>(
                     plan.noteSourceNodeIndices[static_cast<size_t>(scratch->second)])];
                     plan.noteScratchSlots.push_back(scratch->second);
                     plan.notePortFilters.push_back(noteOutputOrdinal(registry.adapterAt(sourceNode.typeIndex), sourcePort));
                  }
                  sourcePlan.noteSources = rangeSince(plan.noteScratchSlots, begin);
               }
            }
            else
            {
               fillPulseScratchSlots(sourcePlan.pulseSources, sourcePlan.hasPulseCable, node.id, -1);
            }

            // An emitter whose Note cables all end in processors is not played itself.
            const auto dests = noteDestsBySource.find(node.id);
            if (dests != noteDestsBySource.end() && !dests->second.empty())
            {
               sourcePlan.reportsToMidiOut = std::any_of(dests->second.begin(), dests->second.end(),
                                                         [&noteProcessorIds](int destId)
                                                         {
                                                            return noteProcessorIds.count(destId) == 0;
                                                         });
            }
         }

         // Appends the note scratch slots (and per-cable output filters) of the Note cables into `destModuleId`.
         auto fillNoteScratchSlots = [&](PlanRange& range, bool& hasNoteCable, int destModuleId)
         {
            const size_t begin = plan.noteScratchSlots.size();
            const auto noteInputs = noteInputsByDest.find(destModuleId);
            if (noteInputs != noteInputsByDest.end())
            {
               hasNoteCable = true;
               for (const auto& [sourceId, sourcePort] : noteInputs->second)
               {
                  auto scratch = noteScratchByModuleId.find(sourceId);
                  if (scratch == noteScratchByModuleId.end())
                     continue;
                  const auto& sourceNode = snapshot.nodes[static_cast<size_t>(
                  plan.noteSourceNodeIndices[static_cast<size_t>(scratch->second)])];
                  plan.noteScratchSlots.push_back(scratch->second);
                  plan.notePortFilters.push_back(noteOutputOrdinal(registry.adapterAt(sourceNode.typeIndex), sourcePort));
               }
            }
            range = rangeSince(plan.noteScratchSlots, begin);
         };

         // Modulation sources run after every note and pulse emitter, so any of them can feed one.
         plan.modulationSourcePlans.resize(plan.modulationSourceNodeIndices.size());
         for (size_t slot = 0; slot < plan.modulationSourceNodeIndices.size(); ++slot)
         {
            EventSourcePlan& sourcePlan = plan.modulationSourcePlans[slot];
            sourcePlan.nodeIndex = plan.modulationSourceNodeIndices[slot];
            const int moduleId = snapshot.nodes[static_cast<size_t>(sourcePlan.nodeIndex)].id;
            fillNoteScratchSlots(sourcePlan.noteSources, sourcePlan.hasNoteCable, moduleId);
            fillPulseScratchSlots(sourcePlan.pulseSources, sourcePlan.hasPulseCable, moduleId, -1);
         }

         plan.steps.reserve(topoModuleIds.size());

         for (int moduleId : topoModuleIds)
         {
            const int nodeIndex = plan.nodeIndexByModuleId[static_cast<size_t>(moduleId)];
            if (nodeIndex < 0)
               continue;

            const auto& node = snapshot.nodes[static_cast<size_t>(nodeIndex)];
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            PlanStep step;
            step.nodeIndex = nodeIndex;
            step.moduleId = moduleId;
            step.typeIndex = node.typeIndex;
            if (adapter)
               step.audioRole = adapter->audioRole();

            const auto audioInputs = audioInputsByDest.find(moduleId);
            {
               const size_t begin = plan.audioInputSlots.size();
               if (audioInputs != audioInputsByDest.end())
               {
                  for (const AudioEdge& edge : audioInputs->second)
                  {
                     auto slotIt = audioSlotByOutput.find(audioOutputKey(edge.sourceId, edge.sourceOutput));
                     if (slotIt == audioSlotByOutput.end())
                        continue;
                     plan.audioInputSlots.push_back(slotIt->second);
                     plan.audioInputPorts.push_back(edge.destPort);
                     plan.audioInputMix.push_back(channelMixFor(adapter, edge.destPort));
                  }
               }
               step.audioInputs = rangeSince(plan.audioInputSlots, begin);
               plan.maxAudioFanIn = std::max(plan.maxAudioFanIn, static_cast<int>(step.audioInputs.count));
            }

            const auto modInputs = modulationInputsByDest.find(moduleId);
            {
               const size_t begin = plan.modulationSourceSlots.size();
               if (modInputs != modulationInputsByDest.end())
               {
                  for (int sourceId : modInputs->second)
                  {
                     auto slotIt = modulationSlotByModuleId.find(sourceId);
                     if (slotIt != modulationSlotByModuleId.end())
                        plan.modulationSourceSlots.push_back(slotIt->second);
                  }
               }
               step.modulationSources = rangeSince(plan.modulationSourceSlots, begin);
            }

            fillNoteScratchSlots(step.noteSources, step.hasNoteCable, moduleId);
            fillPulseScratchSlots(step.pulseSources, step.hasPulseCable, moduleId, -1);

            if (step.audioRole == WasmAudioRole::AudioSource ||
                step.audioRole == WasmAudioRole::AudioProcessor ||
                step.audioRole == WasmAudioRole::Sink)
            {
               // One slot per Audio output; each is released after the last cable from it is read.
               step.outBufferSlot = acquireSlot(step);
               audioSlotByOutput[audioOutputKey(moduleId, 0)] = step.outBufferSlot;
               slotRefCount[step.outBufferSlot] += cablesByAudioOutput[audioOutputKey(moduleId, 0)];

               const size_t auxBegin = plan.auxOutputSlots.size();
               const int outputs = audioOutputCount(adapter);
               for (int ordinal = 1; ordinal < outputs; ++ordinal)
               {
                  const int slot = acquireSlot(step);
                  plan.auxOutputSlots.push_back(slot);
                  audioSlotByOutput[audioOutputKey(moduleId, ordinal)] = slot;
                  slotRefCount[slot] += cablesByAudioOutput[audioOutputKey(moduleId, ordinal)];
               }
               step.auxOutputs = rangeSince(plan.auxOutputSlots, auxBegin);
            }

            if (step.audioRole == WasmAudioRole::Sink)
            {
               plan.outputNodeIndex = nodeIndex;
               plan.outputBufferSlot = step.outBufferSlot;
            }

            plan.steps.push_back(step);

            if (audioInputs != audioInputsByDest.end())
            {
               for (const AudioEdge& edge : audioInputs->second)
               {
                  auto slotIt = audioSlotByOutput.find(audioOutputKey(edge.sourceId, edge.sourceOutput));
                  if (slotIt != audioSlotByOutput.end())
                     releaseSlot(slotIt->second);
               }
            }
         }

         plan.bufferSlotCount = nextSlot;
         plan.valid = plan.outputBufferSlot >= 0;
      }

   } // namespace wasm
} // namespace bespoke
