/**
 * BespokeSynth WASM - Debug RT allocation guard
 */

#include "BespokeWasm/AudioRtGuard.h"

#include <cstdlib>
#include <new>

#if defined(BESPOKE_WASM_RT_ASSERT)

void* operator new(std::size_t size)
{
   if (bespoke::wasm::gAudioCallbackActive.load(std::memory_order_acquire))
      std::abort();
   return std::malloc(size);
}

void operator delete(void* ptr) noexcept
{
   std::free(ptr);
}

void* operator new[](std::size_t size)
{
   if (bespoke::wasm::gAudioCallbackActive.load(std::memory_order_acquire))
      std::abort();
   return std::malloc(size);
}

void operator delete[](void* ptr) noexcept
{
   std::free(ptr);
}

#endif
