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

         template <typename T, int N>
         bool countOk(const SmallVec<T, N>& v)
         {
            return v.count >= 0 && v.count <= N;
         }

         bool eventPlanOk(const EventSourcePlan& source, int nodeCount)
         {
            return source.nodeIndex >= -1 && source.nodeIndex < nodeCount && countOk(source.pulseSourceScratchSlots) &&
                   countOk(source.noteSourceScratchSlots) && countOk(source.noteSourcePortFilters);
         }

         /**
          * The engine indexes nodes and walks SmallVecs straight from the plan (slot indices it
          * range-checks itself), so a damaged snapshot must not reach it.
          */
         bool planIsWellFormed(const AudioGraphSnapshot& graph)
         {
            const AudioProcessPlan& plan = graph.processPlan;
            const int nodeCount = static_cast<int>(graph.nodes.size());
            auto nodeOk = [nodeCount](int index)
            {
               return index >= -1 && index < nodeCount;
            };
            if (!nodeOk(plan.outputNodeIndex) || plan.bufferSlotCount < 0 || plan.modulationSlotCount < 0)
               return false;
            for (const PlanStep& step : plan.steps)
            {
               if (step.nodeIndex < 0 || step.nodeIndex >= nodeCount || !countOk(step.inputBufferSlots) ||
                   !countOk(step.inputBufferPorts) || !countOk(step.modulationSourceSlots) ||
                   !countOk(step.noteSourceSlots) || !countOk(step.noteSourceScratchSlots) ||
                   !countOk(step.noteSourcePortFilters) || !countOk(step.pulseSourceSlots) ||
                   !countOk(step.pulseSourceScratchSlots))
                  return false;
            }
            for (const auto* plans : { &plan.noteSourcePlans, &plan.pulseSourcePlans })
            {
               for (const EventSourcePlan& source : *plans)
               {
                  if (!eventPlanOk(source, nodeCount))
                     return false;
               }
            }
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
         return hash;
      }

      void encodeAudioGraphSnapshot(const AudioGraphSnapshot& graph, std::vector<uint8_t>& out)
      {
         out.clear();
         Writer w(out);
         w.value(kMagic);
         w.value(audioGraphWireAbi());

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
         w.array(plan.steps);
         w.array(plan.modulationSourceNodeIndices);
         w.array(plan.noteSourceNodeIndices);
         w.array(plan.noteSourcePlans);
         w.array(plan.pulseSourceNodeIndices);
         w.array(plan.pulseSourcePlans);
         w.array(plan.nodeIndexByModuleId);
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
         r.value(valid);
         r.value(hasCycle);
         r.value(performanceNotes);
         r.value(outputNodeIndex);
         r.value(outputBufferSlot);
         r.value(bufferSlotCount);
         r.value(modulationSlotCount);
         r.array(plan.steps);
         r.array(plan.modulationSourceNodeIndices);
         r.array(plan.noteSourceNodeIndices);
         r.array(plan.noteSourcePlans);
         r.array(plan.pulseSourceNodeIndices);
         r.array(plan.pulseSourcePlans);
         r.array(plan.nodeIndexByModuleId);
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
