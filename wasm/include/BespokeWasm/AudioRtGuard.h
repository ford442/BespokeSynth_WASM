#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

namespace bespoke::wasm
{
   extern std::atomic<bool> gAudioCallbackActive;

   /**
    * Marks the audio thread's critical section for the real-time checks: any new/delete inside
    * (RtAllocGuard.cpp) or RtMutex lock aborts in the unit tests (BESPOKE_WASM_RT_ASSERT) and is
    * counted in the Debug flavor (BESPOKE_WASM_RT_COUNT, see audioRtViolations). Nests; restores
    * the previous state on exit.
    */
   class AudioRtScope
   {
   public:
      AudioRtScope()
      : mWasActive(gAudioCallbackActive.exchange(true, std::memory_order_acq_rel))
      {
      }
      ~AudioRtScope() { gAudioCallbackActive.store(mWasActive, std::memory_order_release); }

      AudioRtScope(const AudioRtScope&) = delete;
      AudioRtScope& operator=(const AudioRtScope&) = delete;

   private:
      bool mWasActive;
   };

   /** What ran inside an AudioRtScope that real-time code must not do. */
   struct AudioRtViolations
   {
      uint64_t allocations = 0;
      uint64_t frees = 0;
      uint64_t locks = 0;
      /** Allocations and frees are only seen where RtAllocGuard.cpp counts them (Debug flavor). */
      bool countsAllocations = false;
   };

   AudioRtViolations audioRtViolations();
   void resetAudioRtViolations();
   /** RtAllocGuard.cpp (BESPOKE_WASM_RT_COUNT): a heap call inside an AudioRtScope. */
   void countAudioRtAllocation(bool isFree);
   /** RtMutex, before it locks: aborts under BESPOKE_WASM_RT_ASSERT, counted otherwise. */
   void noteAudioRtLock();

   /**
    * A mutex the real-time checks can see. The engine takes none; this proves it stays that way
    * for the stores the UI thread shares with it (SampleStore, ModuleCanvas).
    */
   template <typename Base>
   class RtCheckedMutex : public Base
   {
   public:
      void lock()
      {
         noteAudioRtLock();
         Base::lock();
      }
      bool try_lock()
      {
         noteAudioRtLock();
         return Base::try_lock();
      }
   };

   using RtMutex = RtCheckedMutex<std::mutex>;
   using RtRecursiveMutex = RtCheckedMutex<std::recursive_mutex>;

   /** The JavaScript global scope this module runs in. */
   enum class AudioThreadScope : int
   {
      Unknown = 0,
      Window = 1,
      Worker = 2,
      AudioWorklet = 3,
      Node = 4,
   };

   AudioThreadScope currentAudioThreadScope();
}
