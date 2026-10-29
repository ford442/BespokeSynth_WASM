/**
 * BespokeSynth WASM - AudioGraphSnapshot wire format
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioGraphSnapshotCodec.h"
#include "BespokeWasm/WasmModuleAdapter.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <unordered_map>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr uint32_t kMagic = 0x41475342u; // "BSGA"

         static_assert(std::is_trivially_copyable<AudioGraphNode>::value, "AudioGraphNode is copied verbatim");
         static_assert(std::is_trivially_copyable<AudioGraphConnection>::value, "AudioGraphConnection is copied verbatim");
         static_assert(std::is_trivially_copyable<PlanStep>::value, "PlanStep is copied verbatim");
         static_assert(std::is_trivially_copyable<EventSourcePlan>::value, "EventSourcePlan is copied verbatim");
         static_assert(std::is_trivially_copyable<GraphMusicScale>::value, "GraphMusicScale is copied verbatim");

         class Writer
         {
         public:
            explicit Writer(std::vector<uint8_t>& out)
            : mOut(out)
            {
            }

            void bytes(const void* data, size_t size)
            {
               if (size == 0)
                  return;
               const auto* src = static_cast<const uint8_t*>(data);
               mOut.insert(mOut.end(), src, src + size);
            }

            template <typename T>
            void value(const T& v)
            {
               static_assert(std::is_trivially_copyable<T>::value, "POD only");
               bytes(&v, sizeof(T));
            }

            template <typename T>
            void array(const std::vector<T>& values)
            {
               value(static_cast<uint32_t>(values.size()));
               bytes(values.data(), values.size() * sizeof(T));
            }

            void string(const std::string& s)
            {
               value(static_cast<uint32_t>(s.size()));
               bytes(s.data(), s.size());
            }

         private:
            std::vector<uint8_t>& mOut;
         };

         class Reader
         {
         public:
            Reader(const uint8_t* data, size_t length)
            : mData(data)
            , mLength(length)
            {
            }

            bool ok() const { return mOk; }

            bool bytes(void* dst, size_t size)
            {
               if (!mOk || size > mLength - mPos)
               {
                  mOk = false;
                  return false;
               }
               if (size > 0)
                  std::memcpy(dst, mData + mPos, size);
               mPos += size;
               return true;
            }

            template <typename T>
            bool value(T& v)
            {
               return bytes(&v, sizeof(T));
            }

            template <typename T>
            bool array(std::vector<T>& values)
            {
               uint32_t count = 0;
               // Divide, not multiply: size_t is 32 bits on wasm32, so count * sizeof(T) can wrap.
               if (!value(count) || static_cast<size_t>(count) > (mLength - mPos) / sizeof(T))
               {
                  mOk = false;
                  return false;
               }
               values.resize(count);
               return bytes(values.data(), static_cast<size_t>(count) * sizeof(T));
            }

            bool string(std::string& s)
            {
               uint32_t size = 0;
               if (!value(size) || size > mLength - mPos)
               {
                  mOk = false;
                  return false;
               }
               s.assign(reinterpret_cast<const char*>(mData + mPos), size);
               mPos += size;
               return true;
            }

         private:
            const uint8_t* mData;
            size_t mLength;
            size_t mPos = 0;
            bool mOk = true;
         };

         bool rangeOk(PlanRange range, size_t listSize)
         {
            return range.begin <= listSize && range.count <= listSize - range.begin;
         }

         bool slotsBelow(const std::vector<int>& slots, int limit)
         {
            return std::all_of(slots.begin(), slots.end(), [limit](int slot)
                               {
                                  return slot >= -1 && slot < limit;
                               });
         }

         /**
          * The engine indexes nodes, flat input lists and event scratch straight from the plan,
          * so a damaged snapshot must not reach it.
          */
         bool planIsWellFormed(const AudioGraphSnapshot& graph)
         {
            const AudioProcessPlan& plan = graph.processPlan;
            const int nodeCount = static_cast<int>(graph.nodes.size());
            auto nodeOk = [nodeCount](int index)
            {
               return index >= -1 && index < nodeCount;
            };
            if (!nodeOk(plan.outputNodeIndex) || plan.bufferSlotCount < 0 || plan.modulationSlotCount < 0 ||
                plan.maxAudioFanIn < 0)
               return false;
            const size_t audioInputs = plan.audioInputSlots.size();
            if (plan.audioInputPorts.size() != audioInputs || plan.audioInputMix.size() != audioInputs ||
                plan.notePortFilters.size() != plan.noteScratchSlots.size())
               return false;
            if (!slotsBelow(plan.noteScratchSlots, static_cast<int>(plan.noteSourcePlans.size())) ||
                !slotsBelow(plan.pulseScratchSlots, static_cast<int>(plan.pulseSourceNodeIndices.size())))
               return false;
            for (const PlanStep& step : plan.steps)
            {
               if (step.nodeIndex < 0 || step.nodeIndex >= nodeCount || !rangeOk(step.audioInputs, audioInputs) ||
                   static_cast<int>(step.audioInputs.count) > plan.maxAudioFanIn ||
                   !rangeOk(step.modulationSources, plan.modulationSourceSlots.size()) ||
                   !rangeOk(step.noteSources, plan.noteScratchSlots.size()) ||
                   !rangeOk(step.pulseSources, plan.pulseScratchSlots.size()) ||
                   !rangeOk(step.auxOutputs, plan.auxOutputSlots.size()))
                  return false;
            }
            if (!slotsBelow(plan.auxOutputSlots, plan.bufferSlotCount))
               return false;
            for (const auto* plans : { &plan.noteSourcePlans, &plan.pulseSourcePlans, &plan.modulationSourcePlans })
            {
               for (const EventSourcePlan& source : *plans)
               {
                  if (!nodeOk(source.nodeIndex) || !rangeOk(source.noteSources, plan.noteScratchSlots.size()) ||
                      !rangeOk(source.pulseSources, plan.pulseScratchSlots.size()))
                     return false;
               }
            }
            if (plan.noteSourcePlans.size() != plan.noteSourceNodeIndices.size() ||
                plan.modulationSourcePlans.size() != plan.modulationSourceNodeIndices.size())
               return false;
            for (const auto* indices : { &plan.modulationSourceNodeIndices, &plan.noteSourceNodeIndices,
                                         &plan.pulseSourceNodeIndices, &plan.nodeIndexByModuleId })
            {
               if (!std::all_of(indices->begin(), indices->end(), nodeOk))
                  return false;
            }
            return true;
         }

         uint32_t mixAbi(uint32_t hash, uint32_t value)
         {
            // FNV-1a over 32-bit words; only needs to differ when a layout does.
            hash ^= value;
            return hash * 16777619u;
         }
      }

      uint32_t audioGraphWireAbi()
      {
         uint32_t hash = 2166136261u;
         hash = mixAbi(hash, kAudioGraphWireVersion);
         hash = mixAbi(hash, sizeof(AudioGraphNode));
         hash = mixAbi(hash, sizeof(AudioGraphConnection));
         hash = mixAbi(hash, sizeof(PlanStep));
         hash = mixAbi(hash, sizeof(EventSourcePlan));
         hash = mixAbi(hash, sizeof(GraphMusicScale));
         hash = mixAbi(hash, sizeof(WasmNoteEvent));
         hash = mixAbi(hash, sizeof(PlanRange));
         hash = mixAbi(hash, sizeof(PlanChannelMix));
         return hash;
      }

      void encodeAudioGraphSnapshot(const AudioGraphSnapshot& graph, std::vector<uint8_t>& out)
      {
         out.clear();
         Writer w(out);
         w.value(kMagic);
         w.value(audioGraphWireAbi());
         w.value(graph.generation);
         w.value(graph.paramSeq);

         w.value(static_cast<uint8_t>(graph.transportPlaying ? 1 : 0));
         w.value(graph.transportBPM);
         w.value(graph.musicScale);

         // Type ids for every type index the snapshot uses, keyed by the sender's index.
         auto& registry = WasmModuleAdapterRegistry::instance();
         std::vector<int> typeIndices;
         for (const auto& node : graph.nodes)
         {
            if (node.typeIndex >= 0 &&
                std::find(typeIndices.begin(), typeIndices.end(), node.typeIndex) == typeIndices.end())
               typeIndices.push_back(node.typeIndex);
         }
         w.value(static_cast<uint32_t>(typeIndices.size()));
         for (int typeIndex : typeIndices)
         {
            const WasmModuleAdapter* adapter = registry.adapterAt(typeIndex);
            w.value(static_cast<int32_t>(typeIndex));
            w.string(adapter ? adapter->typeId() : "");
         }

         w.array(graph.nodes);
         w.array(graph.connections);
         w.array(graph.paramArena);

         const AudioProcessPlan& plan = graph.processPlan;
         w.value(static_cast<uint8_t>(plan.valid ? 1 : 0));
         w.value(static_cast<uint8_t>(plan.hasCycle ? 1 : 0));
         w.value(static_cast<uint8_t>(plan.performanceNotesProcessed ? 1 : 0));
         w.value(static_cast<int32_t>(plan.outputNodeIndex));
         w.value(static_cast<int32_t>(plan.outputBufferSlot));
         w.value(static_cast<int32_t>(plan.bufferSlotCount));
         w.value(static_cast<int32_t>(plan.modulationSlotCount));
         w.value(static_cast<int32_t>(plan.maxAudioFanIn));
         w.array(plan.steps);
         w.array(plan.modulationSourceNodeIndices);
         w.array(plan.noteSourceNodeIndices);
         w.array(plan.noteSourcePlans);
         w.array(plan.pulseSourceNodeIndices);
         w.array(plan.pulseSourcePlans);
         w.array(plan.nodeIndexByModuleId);
         w.array(plan.audioInputSlots);
         w.array(plan.audioInputPorts);
         w.array(plan.audioInputMix);
         w.array(plan.modulationSourceSlots);
         w.array(plan.noteScratchSlots);
         w.array(plan.notePortFilters);
         w.array(plan.pulseScratchSlots);
         w.array(plan.auxOutputSlots);
         w.array(plan.modulationSourcePlans);
      }

      bool decodeAudioGraphSnapshot(const uint8_t* bytes,
                                    size_t length,
                                    AudioGraphSnapshot& graph,
                                    std::string& error)
      {
         if (!bytes)
         {
            error = "no snapshot bytes";
            return false;
         }
         Reader r(bytes, length);
         uint32_t magic = 0;
         uint32_t abi = 0;
         if (!r.value(magic) || magic != kMagic)
         {
            error = "not an audio graph snapshot";
            return false;
         }
         if (!r.value(abi) || abi != audioGraphWireAbi())
         {
            error = "snapshot ABI mismatch (UI and audio modules are from different builds)";
            return false;
         }

         AudioGraphSnapshot decoded;
         r.value(decoded.generation);
         r.value(decoded.paramSeq);
         uint8_t playing = 0;
         r.value(playing);
         decoded.transportPlaying = playing != 0;
         r.value(decoded.transportBPM);
         r.value(decoded.musicScale);

         auto& registry = WasmModuleAdapterRegistry::instance();
         std::unordered_map<int, int> localTypeIndex;
         uint32_t typeCount = 0;
         r.value(typeCount);
         for (uint32_t i = 0; i < typeCount && r.ok(); ++i)
         {
            int32_t remoteIndex = -1;
            std::string typeId;
            r.value(remoteIndex);
            r.string(typeId);
            localTypeIndex[remoteIndex] = registry.typeIndexFor(typeId);
         }
         auto mapType = [&localTypeIndex](int remoteIndex)
         {
            auto it = localTypeIndex.find(remoteIndex);
            return it == localTypeIndex.end() ? -1 : it->second;
         };

         r.array(decoded.nodes);
         r.array(decoded.connections);
         r.array(decoded.paramArena);

         AudioProcessPlan& plan = decoded.processPlan;
         uint8_t valid = 0;
         uint8_t hasCycle = 0;
         uint8_t performanceNotes = 0;
         int32_t outputNodeIndex = -1;
         int32_t outputBufferSlot = -1;
         int32_t bufferSlotCount = 0;
         int32_t modulationSlotCount = 0;
         int32_t maxAudioFanIn = 0;
         r.value(valid);
         r.value(hasCycle);
         r.value(performanceNotes);
         r.value(outputNodeIndex);
         r.value(outputBufferSlot);
         r.value(bufferSlotCount);
         r.value(modulationSlotCount);
         r.value(maxAudioFanIn);
         r.array(plan.steps);
         r.array(plan.modulationSourceNodeIndices);
         r.array(plan.noteSourceNodeIndices);
         r.array(plan.noteSourcePlans);
         r.array(plan.pulseSourceNodeIndices);
         r.array(plan.pulseSourcePlans);
         r.array(plan.nodeIndexByModuleId);
         r.array(plan.audioInputSlots);
         r.array(plan.audioInputPorts);
         r.array(plan.audioInputMix);
         r.array(plan.modulationSourceSlots);
         r.array(plan.noteScratchSlots);
         r.array(plan.notePortFilters);
         r.array(plan.pulseScratchSlots);
         r.array(plan.auxOutputSlots);
         r.array(plan.modulationSourcePlans);
         if (!r.ok())
         {
            error = "truncated audio graph snapshot";
            return false;
         }
         plan.valid = valid != 0;
         plan.hasCycle = hasCycle != 0;
         plan.performanceNotesProcessed = performanceNotes != 0;
         plan.outputNodeIndex = outputNodeIndex;
         plan.outputBufferSlot = outputBufferSlot;
         plan.bufferSlotCount = bufferSlotCount;
         plan.modulationSlotCount = modulationSlotCount;
         plan.maxAudioFanIn = maxAudioFanIn;

         for (auto& node : decoded.nodes)
         {
            node.typeIndex = mapType(node.typeIndex);
            const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex);
            if (!adapter)
               continue; // a type this build lacks plays silence, like a placeholder
            // A param-less node (transport, scale) still gets an aligned offset, possibly past the arena end.
            if (node.paramSize != adapter->paramsSize() ||
                (node.paramSize > 0 &&
                 static_cast<size_t>(node.paramOffset) + node.paramSize > decoded.paramArena.size()))
            {
               error = std::string("params size mismatch for ") + adapter->typeId();
               return false;
            }
            if (void* params = decoded.paramsFor(node))
               adapter->rebindParams(node.id, params);
         }
         for (auto& step : plan.steps)
            step.typeIndex = mapType(step.typeIndex);
         if (!planIsWellFormed(decoded))
         {
            error = "malformed audio graph snapshot";
            return false;
         }

         graph = std::move(decoded);
         return true;
      }

   } // namespace wasm
} // namespace bespoke
