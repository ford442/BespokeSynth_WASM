#include "BespokeWasm/AudioRtGuard.h"

#include <cstdlib>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Bracket access: Closure (Release) does not know the worklet and worker scope globals.
// clang-format off
EM_JS(int, bespoke_js_global_scope, (), {
   var g = globalThis;
   var worklet = g['AudioWorkletGlobalScope'];
   if (worklet && g instanceof worklet) return 3;
   var worker = g['WorkerGlobalScope'];
   if (worker && g instanceof worker) return 2;
   if (g['window'] === g) return 1;
   if (g['process'] && g['process']['versions'] && g['process']['versions']['node']) return 4;
   return 0;
});
// clang-format on
#endif

namespace bespoke::wasm
{
   std::atomic<bool> gAudioCallbackActive{ false };

   namespace
   {
      std::atomic<uint64_t> gRtAllocations{ 0 };
      std::atomic<uint64_t> gRtFrees{ 0 };
      std::atomic<uint64_t> gRtLocks{ 0 };
   }

   AudioRtViolations audioRtViolations()
   {
      AudioRtViolations v;
      v.allocations = gRtAllocations.load(std::memory_order_relaxed);
      v.frees = gRtFrees.load(std::memory_order_relaxed);
      v.locks = gRtLocks.load(std::memory_order_relaxed);
#if defined(BESPOKE_WASM_RT_COUNT) || defined(BESPOKE_WASM_RT_ASSERT)
      v.countsAllocations = true;
#endif
      return v;
   }

   void resetAudioRtViolations()
   {
      gRtAllocations.store(0, std::memory_order_relaxed);
      gRtFrees.store(0, std::memory_order_relaxed);
      gRtLocks.store(0, std::memory_order_relaxed);
   }

   void countAudioRtAllocation(bool isFree)
   {
      (isFree ? gRtFrees : gRtAllocations).fetch_add(1, std::memory_order_relaxed);
   }

   void noteAudioRtLock()
   {
      if (!gAudioCallbackActive.load(std::memory_order_acquire))
         return;
#if defined(BESPOKE_WASM_RT_ASSERT)
      std::abort();
#else
      gRtLocks.fetch_add(1, std::memory_order_relaxed);
#endif
   }

   AudioThreadScope currentAudioThreadScope()
   {
#ifdef __EMSCRIPTEN__
      return static_cast<AudioThreadScope>(bespoke_js_global_scope());
#else
      return AudioThreadScope::Unknown;
#endif
   }
}
