/**
 * BespokeSynth WASM - Looper arena pool
 */

#include "BespokeWasm/LooperArena.h"

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
         if (moduleId >= static_cast<int>(mArenas.size()))
            mArenas.resize(static_cast<size_t>(moduleId) + 1);
         if (!mArenas[static_cast<size_t>(moduleId)])
            mArenas[static_cast<size_t>(moduleId)] = std::make_unique<LooperArena>();
         return mArenas[static_cast<size_t>(moduleId)].get();
      }

      LooperArena* LooperArenaPool::find(int moduleId)
      {
         if (moduleId < 0 || moduleId >= static_cast<int>(mArenas.size()))
            return nullptr;
         return mArenas[static_cast<size_t>(moduleId)].get();
      }

      void LooperArenaPool::release(int moduleId)
      {
         if (moduleId < 0 || moduleId >= static_cast<int>(mArenas.size()))
            return;
         mArenas[static_cast<size_t>(moduleId)].reset();
      }

   } // namespace wasm
} // namespace bespoke
