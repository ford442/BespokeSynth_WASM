/**
 * BespokeSynth WASM - Looper arena pool
 */

#include "BespokeWasm/LooperArena.h"
#include <algorithm>

namespace bespoke
{
   namespace wasm
   {
      LooperArenaPool& LooperArenaPool::instance()
      {
         static LooperArenaPool pool;
         return pool;
      }

      LooperArena* LooperArenaPool::ensure(int moduleId)
      {
         if (moduleId < 0)
            return nullptr;
         if (moduleId >= static_cast<int>(mSlots.size()))
            mSlots.resize(static_cast<size_t>(moduleId) + 1);
         Slot& slot = mSlots[static_cast<size_t>(moduleId)];
         if (!slot.arena)
         {
            slot.arena = std::make_unique<LooperArena>();
            slot.pinned = true; // until a snapshot published after this leaves it out
            slot.lastUse = mLastRetainGeneration;
         }
         return slot.arena.get();
      }

      LooperArena* LooperArenaPool::find(int moduleId)
      {
         if (moduleId < 0 || moduleId >= static_cast<int>(mSlots.size()))
            return nullptr;
         return mSlots[static_cast<size_t>(moduleId)].arena.get();
      }

      void LooperArenaPool::release(int moduleId)
      {
         evictId(moduleId);
      }

      void LooperArenaPool::retainOnly(std::span<const int> referencedModuleIds, uint64_t generation)
      {
         mLastRetainGeneration = generation;
         for (size_t id = 0; id < mSlots.size(); ++id)
         {
            Slot& slot = mSlots[id];
            if (!slot.arena)
               continue;
            const bool referenced = std::find(referencedModuleIds.begin(), referencedModuleIds.end(),
                                              static_cast<int>(id)) != referencedModuleIds.end();
            if (referenced)
            {
               slot.pinned = true;
               slot.lastUse = generation;
            }
            else if (slot.pinned)
            {
               slot.pinned = false;
               slot.unpinnedAt = generation;
            }
         }
      }

      int LooperArenaPool::evictionCandidate(uint64_t safeGeneration, uint64_t& lastUse) const
      {
         int best = -1;
         for (size_t id = 0; id < mSlots.size(); ++id)
         {
            const Slot& slot = mSlots[id];
            if (!slot.arena || slot.pinned || slot.unpinnedAt > safeGeneration)
               continue;
            if (best < 0 || slot.lastUse < mSlots[static_cast<size_t>(best)].lastUse)
               best = static_cast<int>(id);
         }
         if (best >= 0)
            lastUse = mSlots[static_cast<size_t>(best)].lastUse;
         return best;
      }

      size_t LooperArenaPool::evictId(int moduleId)
      {
         if (moduleId < 0 || moduleId >= static_cast<int>(mSlots.size()) || !mSlots[static_cast<size_t>(moduleId)].arena)
            return 0;
         mSlots[static_cast<size_t>(moduleId)].arena.reset();
         return kArenaBytes;
      }

      size_t LooperArenaPool::residentBytes() const
      {
         size_t bytes = 0;
         for (const Slot& slot : mSlots)
            bytes += slot.arena ? kArenaBytes : 0;
         return bytes;
      }

   } // namespace wasm
} // namespace bespoke
