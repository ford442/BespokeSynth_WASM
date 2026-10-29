/**
 * BespokeSynth WASM - Deferred reclamation of sample buffers and looper arenas
 */

#include "BespokeWasm/ResourceLifetime.h"
#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/LooperArena.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <atomic>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         std::atomic<size_t> gResourceBudget{ kDefaultResourceBudget };
      }

      WasmResourceRefs collectSnapshotResources(const AudioGraphSnapshot& graph)
      {
         WasmResourceRefs refs;
         auto& registry = WasmModuleAdapterRegistry::instance();
         for (const auto& node : graph.nodes)
         {
            if (const WasmModuleAdapter* adapter = registry.adapterAt(node.typeIndex))
               adapter->collectResources(node.id, graph.paramsFor(node), refs);
         }
         return refs;
      }

      void retainResources(const WasmResourceRefs& refs, uint64_t generation)
      {
         SampleStore::instance().retainOnly(refs.sampleIds, generation);
         LooperArenaPool::instance().retainOnly(refs.looperModuleIds, generation);
      }

      size_t residentResourceBytes()
      {
         return SampleStore::instance().residentBytes() + LooperArenaPool::instance().residentBytes();
      }

      size_t collectUnusedResources(uint64_t safeGeneration)
      {
         auto& samples = SampleStore::instance();
         auto& arenas = LooperArenaPool::instance();
         const size_t budget = resourceBudget();
         size_t freed = 0;
         while (residentResourceBytes() > budget)
         {
            uint64_t sampleUse = 0;
            uint64_t arenaUse = 0;
            const int sample = samples.evictionCandidate(safeGeneration, sampleUse);
            const int arena = arenas.evictionCandidate(safeGeneration, arenaUse);
            if (sample < 0 && arena < 0)
               break;
            if (sample >= 0 && (arena < 0 || sampleUse <= arenaUse))
               freed += samples.evictId(sample);
            else
               freed += arenas.evictId(arena);
         }
         return freed;
      }

      void setResourceBudget(size_t bytes)
      {
         gResourceBudget.store(bytes, std::memory_order_relaxed);
      }

      size_t resourceBudget()
      {
         return gResourceBudget.load(std::memory_order_relaxed);
      }

   } // namespace wasm
} // namespace bespoke
