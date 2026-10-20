/**
 * BespokeSynth WASM - Deferred reclamation of sample buffers and looper arenas
 *
 * Snapshots point at store-owned memory (SamplerParams::sample, LooperParams::arena).
 * Each publish pins what the new snapshot names (retainResources); memory a snapshot
 * stopped naming is freed by collectUnusedResources, least recently used first, but only
 * once the engine can no longer process a snapshot that named it: anything unpinned at or
 * before `safeGeneration`. Patch data stays in OPFS / the resource pack, so an evicted
 * sample is loaded again by hash when a snapshot needs it (undo, patch load).
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bespoke
{
   namespace wasm
   {
      struct AudioGraphSnapshot;

      /** Store memory a snapshot keeps alive (WasmModuleAdapter::collectResources). */
      struct WasmResourceRefs
      {
         std::vector<int> sampleIds;
         std::vector<int> looperModuleIds;
      };

      [[nodiscard]] WasmResourceRefs collectSnapshotResources(const AudioGraphSnapshot& graph);
      /** Pins `refs` in both stores as of snapshot `generation`; unpins everything else. */
      void retainResources(const WasmResourceRefs& refs, uint64_t generation);
      /**
       * Frees unpinned samples and arenas (unpinned at or before `safeGeneration`), least recently
       * used first, until both stores together fit the budget. Never from processBlock. Returns the
       * bytes freed.
       */
      size_t collectUnusedResources(uint64_t safeGeneration);
      size_t residentResourceBytes();

      constexpr size_t kDefaultResourceBudget = size_t{ 256 } << 20;
      void setResourceBudget(size_t bytes);
      [[nodiscard]] size_t resourceBudget();

   } // namespace wasm
} // namespace bespoke
