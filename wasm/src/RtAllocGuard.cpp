/**
 * BespokeSynth WASM - RT allocation guard
 *
 * Replaces every global allocation and deallocation overload (aligned ones included) and
 * checks gAudioCallbackActive (AudioRtScope, armed by AudioGraphEngine::processBlock):
 *  - BESPOKE_WASM_RT_ASSERT (unit tests): the first heap touch in the audio path aborts.
 *  - BESPOKE_WASM_RT_COUNT (Debug flavor of the app and the AudioWorklet engine): it is
 *    counted instead (audioRtViolations, health JSON and the engine's status block).
 * Freeing nullptr stays allowed (moved-from members, empty vectors).
 */

#include "BespokeWasm/AudioRtGuard.h"

#include <algorithm>
#include <cstdlib>
#include <new>

#if defined(BESPOKE_WASM_RT_ASSERT) || defined(BESPOKE_WASM_RT_COUNT)

namespace
{
   void violation(bool isFree)
   {
#if defined(BESPOKE_WASM_RT_ASSERT)
      (void)isFree;
      std::abort();
#else
      bespoke::wasm::countAudioRtAllocation(isFree);
#endif
   }

   void checkAllocation()
   {
      if (bespoke::wasm::gAudioCallbackActive.load(std::memory_order_acquire))
         violation(false);
   }

   void checkRelease(void* ptr)
   {
      if (ptr && bespoke::wasm::gAudioCallbackActive.load(std::memory_order_acquire))
         violation(true);
   }

   void* allocate(std::size_t size)
   {
      checkAllocation();
      void* ptr = std::malloc(size ? size : 1);
      if (!ptr)
         std::abort();
      return ptr;
   }

   void* allocateAligned(std::size_t size, std::align_val_t alignment)
   {
      checkAllocation();
      const std::size_t align = std::max(static_cast<std::size_t>(alignment), sizeof(void*));
      void* ptr = std::aligned_alloc(align, (std::max<std::size_t>(size, 1) + align - 1) / align * align);
      if (!ptr)
         std::abort();
      return ptr;
   }

   void release(void* ptr) noexcept
   {
      checkRelease(ptr);
      std::free(ptr);
   }
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return allocateAligned(size, alignment); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return allocateAligned(size, alignment); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
   return allocateAligned(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
   return allocateAligned(size, alignment);
}

void operator delete(void* ptr) noexcept { release(ptr); }
void operator delete[](void* ptr) noexcept { release(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { release(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { release(ptr); }
void operator delete(void* ptr, const std::nothrow_t&) noexcept { release(ptr); }
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { release(ptr); }
void operator delete(void* ptr, std::align_val_t) noexcept { release(ptr); }
void operator delete[](void* ptr, std::align_val_t) noexcept { release(ptr); }
void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept { release(ptr); }
void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept { release(ptr); }
void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept { release(ptr); }
void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept { release(ptr); }

#endif
