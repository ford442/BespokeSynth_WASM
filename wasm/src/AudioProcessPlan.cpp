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
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
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
          * Order pulse sources so pulse processors (e.g. clockdivider) run after the sources feeding
          * them. Nodes caught in a pulse cycle keep their original relative order; the edge that
          * closes the cycle is ignored by the engine (it only reads earlier scratch slots).
          */
         std::vector<int> orderPulseSources(const AudioGraphSnapshot& snapshot,
                                            const std::vector<int>& nodeIndices,
                                            const std::unordered_map<int, std::vector<std::pair<int, int>>>& pulseInputsByDest)
         {
            std::unordered_map<int, int> positionByModuleId;
            for (int i = 0; i < static_cast<int>(nodeIndices.size()); ++i)
               positionByModuleId[snapshot.nodes[static_cast<size_t>(nodeIndices[static_cast<size_t>(i)])].id] = i;

            std::vector<int> indegree(nodeIndices.size(), 0);
            std::vector<std::vector<int>> children(nodeIndices.size());
            for (int i = 0; i < static_cast<int>(nodeIndices.size()); ++i)
            {
               const int moduleId = snapshot.nodes[static_cast<size_t>(nodeIndices[static_cast<size_t>(i)])].id;
               auto inputs = pulseInputsByDest.find(moduleId);
               if (inputs == pulseInputsByDest.end())
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

      void compileAudioProcessPlan(const AudioGraphSnapshot& snapshot, AudioProcessPlan& plan)
      {
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

         // (source module id, port) pairs: dest input port for audio, source output port for note/pulse.
         std::unordered_map<int, std::vector<std::pair<int, int>>> audioInputsByDest;
         std::unordered_map<int, std::vector<int>> modulationInputsByDest;
         std::unordered_map<int, std::vector<std::pair<int, int>>> noteInputsByDest;
         std::unordered_map<int, std::vector<std::pair<int, int>>> pulseInputsByDest;
         std::unordered_map<int, std::vector<int>> audioChildrenBySource;
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
            if (adapter->audioRole() == WasmAudioRole::NoteSource)
            {
               plan.noteSourceNodeIndices.push_back(static_cast<int>(nodeIndex));
               EventSourcePlan sourcePlan;
               sourcePlan.nodeIndex = static_cast<int>(nodeIndex);
               plan.noteSourcePlans.push_back(sourcePlan);
            }
            if (adapter->audioRole() == WasmAudioRole::PulseSource)
               plan.pulseSourceNodeIndices.push_back(static_cast<int>(nodeIndex));
         }

         for (const auto& conn : snapshot.connections)
         {
            if (conn.sourcePortType == PortType::Audio && conn.destPortType == PortType::Audio &&
                audioModuleIds.count(conn.sourceModuleId) && audioModuleIds.count(conn.destModuleId))
            {
               audioInputsByDest[conn.destModuleId].push_back({ conn.sourceModuleId, conn.destPortIndex });
               audioChildrenBySource[conn.sourceModuleId].push_back(conn.destModuleId);
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

         std::unordered_map<int, int> audioSlotByModuleId;
         std::unordered_map<int, int> slotRefCount;
         std::vector<int> freeSlots;
         int nextSlot = 0;

         auto acquireSlot = [&](const PlanStep& step) -> int
         {
            for (auto it = freeSlots.begin(); it != freeSlots.end(); ++it)
            {
               bool usedAsInput = false;
               for (int inputSlot : step.inputBufferSlots)
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

         plan.pulseSourceNodeIndices = orderPulseSources(snapshot, plan.pulseSourceNodeIndices, pulseInputsByDest);

         std::unordered_map<int, int> pulseScratchByModuleId;
         for (int scratch = 0; scratch < static_cast<int>(plan.pulseSourceNodeIndices.size()); ++scratch)
         {
            const int nodeIndex = plan.pulseSourceNodeIndices[static_cast<size_t>(scratch)];
            pulseScratchByModuleId[snapshot.nodes[static_cast<size_t>(nodeIndex)].id] = scratch;
         }

         auto fillPulseScratchSlots = [&](SmallVec<int, 4>& slots, bool& hasPulseCable, int destModuleId)
         {
            const auto pulseInputs = pulseInputsByDest.find(destModuleId);
            if (pulseInputs == pulseInputsByDest.end())
               return;
            hasPulseCable = true;
            for (const auto& [sourceId, sourcePort] : pulseInputs->second)
            {
               (void)sourcePort;
               auto it = pulseScratchByModuleId.find(sourceId);
               if (it != pulseScratchByModuleId.end())
                  slots.push_back(it->second);
            }
         };

         plan.pulseSourcePlans.resize(plan.pulseSourceNodeIndices.size());
         for (int scratch = 0; scratch < static_cast<int>(plan.pulseSourceNodeIndices.size()); ++scratch)
         {
            EventSourcePlan& sourcePlan = plan.pulseSourcePlans[static_cast<size_t>(scratch)];
            sourcePlan.nodeIndex = plan.pulseSourceNodeIndices[static_cast<size_t>(scratch)];
            const int destId = snapshot.nodes[static_cast<size_t>(sourcePlan.nodeIndex)].id;
            SmallVec<int, 4> upstream;
            fillPulseScratchSlots(upstream, sourcePlan.hasPulseCable, destId);
            // Only sources evaluated earlier this block can feed this one (cycles are cut here).
            for (int slot : upstream)
            {
               if (slot < scratch)
                  sourcePlan.pulseSourceScratchSlots.push_back(slot);
            }
         }

         for (auto& sourcePlan : plan.noteSourcePlans)
         {
            const int destId = snapshot.nodes[static_cast<size_t>(sourcePlan.nodeIndex)].id;
            fillPulseScratchSlots(sourcePlan.pulseSourceScratchSlots, sourcePlan.hasPulseCable, destId);
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
            if (audioInputs != audioInputsByDest.end())
            {
               for (const auto& [sourceId, destPort] : audioInputs->second)
               {
                  auto slotIt = audioSlotByModuleId.find(sourceId);
                  if (slotIt != audioSlotByModuleId.end() && !step.inputBufferSlots.full())
                  {
                     step.inputBufferSlots.push_back(slotIt->second);
                     step.inputBufferPorts.push_back(destPort);
                  }
               }
            }

            const auto modInputs = modulationInputsByDest.find(moduleId);
            if (modInputs != modulationInputsByDest.end())
            {
               for (int sourceId : modInputs->second)
               {
                  auto slotIt = modulationSlotByModuleId.find(sourceId);
                  if (slotIt != modulationSlotByModuleId.end())
                     step.modulationSourceSlots.push_back(slotIt->second);
               }
            }

            const auto noteInputs = noteInputsByDest.find(moduleId);
            if (noteInputs != noteInputsByDest.end())
            {
               step.hasNoteCable = true;
               for (const auto& [sourceId, sourcePort] : noteInputs->second)
               {
                  if (sourceId < 0 || sourceId >= static_cast<int>(plan.nodeIndexByModuleId.size()))
                     continue;
                  const int sourceNodeIndex = plan.nodeIndexByModuleId[static_cast<size_t>(sourceId)];
                  if (sourceNodeIndex < 0)
                     continue;
                  step.noteSourceSlots.push_back(sourceNodeIndex);
                  for (int scratch = 0; scratch < static_cast<int>(plan.noteSourceNodeIndices.size()); ++scratch)
                  {
                     if (plan.noteSourceNodeIndices[static_cast<size_t>(scratch)] == sourceNodeIndex)
                     {
                        if (!step.noteSourceScratchSlots.full())
                        {
                           const auto& sourceNode = snapshot.nodes[static_cast<size_t>(sourceNodeIndex)];
                           step.noteSourceScratchSlots.push_back(scratch);
                           step.noteSourcePortFilters.push_back(
                           noteOutputOrdinal(registry.adapterAt(sourceNode.typeIndex), sourcePort));
                        }
                        break;
                     }
                  }
               }
            }

            fillPulseScratchSlots(step.pulseSourceScratchSlots, step.hasPulseCable, moduleId);
            const auto pulseInputs = pulseInputsByDest.find(moduleId);
            if (pulseInputs != pulseInputsByDest.end())
            {
               for (const auto& [sourceId, sourcePort] : pulseInputs->second)
               {
                  (void)sourcePort;
                  if (sourceId < 0 || sourceId >= static_cast<int>(plan.nodeIndexByModuleId.size()))
                     continue;
                  const int sourceNodeIndex = plan.nodeIndexByModuleId[static_cast<size_t>(sourceId)];
                  if (sourceNodeIndex >= 0)
                     step.pulseSourceSlots.push_back(sourceNodeIndex);
               }
            }

            if (step.audioRole == WasmAudioRole::AudioSource ||
                step.audioRole == WasmAudioRole::AudioProcessor ||
                step.audioRole == WasmAudioRole::Sink)
            {
               step.outBufferSlot = acquireSlot(step);
               audioSlotByModuleId[moduleId] = step.outBufferSlot;

               for (int child : audioChildrenBySource[moduleId])
                  ++slotRefCount[step.outBufferSlot];
            }

            if (step.audioRole == WasmAudioRole::Sink)
            {
               plan.outputNodeIndex = nodeIndex;
               plan.outputBufferSlot = step.outBufferSlot;
            }

            plan.steps.push_back(step);

            if (audioInputs != audioInputsByDest.end())
            {
               for (const auto& [sourceId, destPort] : audioInputs->second)
               {
                  (void)destPort;
                  auto slotIt = audioSlotByModuleId.find(sourceId);
                  if (slotIt != audioSlotByModuleId.end())
                     releaseSlot(slotIt->second);
               }
            }
         }

         plan.bufferSlotCount = nextSlot;
         plan.valid = plan.outputBufferSlot >= 0;
      }

   } // namespace wasm
} // namespace bespoke
