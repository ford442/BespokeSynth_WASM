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

         std::unordered_map<int, std::vector<int>> audioInputsByDest;
         std::unordered_map<int, std::vector<int>> modulationInputsByDest;
         std::unordered_map<int, std::vector<int>> noteInputsByDest;
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
               plan.noteSourceNodeIndices.push_back(static_cast<int>(nodeIndex));
         }

         for (const auto& conn : snapshot.connections)
         {
            if (conn.sourcePortType == PortType::Audio && conn.destPortType == PortType::Audio &&
                audioModuleIds.count(conn.sourceModuleId) && audioModuleIds.count(conn.destModuleId))
            {
               audioInputsByDest[conn.destModuleId].push_back(conn.sourceModuleId);
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
               noteInputsByDest[conn.destModuleId].push_back(conn.sourceModuleId);
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
               for (int sourceId : audioInputs->second)
               {
                  auto slotIt = audioSlotByModuleId.find(sourceId);
                  if (slotIt != audioSlotByModuleId.end())
                     step.inputBufferSlots.push_back(slotIt->second);
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
               for (int sourceId : noteInputs->second)
               {
                  const int sourceNodeIndex = plan.nodeIndexByModuleId[static_cast<size_t>(sourceId)];
                  if (sourceNodeIndex < 0)
                     continue;
                  step.noteSourceSlots.push_back(sourceNodeIndex);
                  for (int scratch = 0; scratch < static_cast<int>(plan.noteSourceNodeIndices.size()); ++scratch)
                  {
                     if (plan.noteSourceNodeIndices[static_cast<size_t>(scratch)] == sourceNodeIndex)
                     {
                        step.noteSourceScratchSlots.push_back(scratch);
                        break;
                     }
                  }
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
               for (int sourceId : audioInputs->second)
               {
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
