/**
 * BespokeSynth WASM - Lock-free note queues for Script modules
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ScriptNoteQueue.h"

#include <array>
#include <atomic>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         struct ScriptQueue
         {
            std::atomic<int> moduleId{ -1 };
            std::atomic<uint32_t> writeIndex{ 0 };
            std::atomic<uint32_t> readIndex{ 0 };
            std::array<WasmNoteEvent, kScriptQueueCapacity> ring{};
         };

         std::array<ScriptQueue, kMaxScriptQueues>& queues()
         {
            static std::array<ScriptQueue, kMaxScriptQueues> table;
            return table;
         }

         ScriptQueue* findQueue(int moduleId)
         {
            for (auto& queue : queues())
            {
               if (queue.moduleId.load(std::memory_order_acquire) == moduleId)
                  return &queue;
            }
            return nullptr;
         }
      }

      bool pushScriptNote(int moduleId, const WasmNoteEvent& note)
      {
         if (moduleId < 0)
            return false;
         ScriptQueue* queue = findQueue(moduleId);
         if (!queue)
         {
            queue = findQueue(-1);
            if (!queue)
               return false;
            queue->readIndex.store(0, std::memory_order_relaxed);
            queue->writeIndex.store(0, std::memory_order_relaxed);
            queue->moduleId.store(moduleId, std::memory_order_release);
         }
         const uint32_t write = queue->writeIndex.load(std::memory_order_relaxed);
         const uint32_t next = (write + 1) % kScriptQueueCapacity;
         if (next == queue->readIndex.load(std::memory_order_acquire))
            return false;
         queue->ring[write] = note;
         queue->writeIndex.store(next, std::memory_order_release);
         return true;
      }

      int drainScriptNotes(int moduleId, WasmNoteEvent* out, int maxNotes)
      {
         ScriptQueue* queue = moduleId >= 0 ? findQueue(moduleId) : nullptr;
         if (!queue || !out)
            return 0;
         int count = 0;
         while (count < maxNotes)
         {
            const uint32_t read = queue->readIndex.load(std::memory_order_relaxed);
            if (read == queue->writeIndex.load(std::memory_order_acquire))
               break;
            out[count++] = queue->ring[read];
            queue->readIndex.store((read + 1) % kScriptQueueCapacity, std::memory_order_release);
         }
         return count;
      }

      void clearScriptNotes(int moduleId)
      {
         if (moduleId < 0)
            return;
         if (ScriptQueue* queue = findQueue(moduleId))
         {
            queue->readIndex.store(queue->writeIndex.load(std::memory_order_acquire), std::memory_order_release);
            queue->moduleId.store(-1, std::memory_order_release);
         }
      }

      void resetScriptNoteQueues()
      {
         for (auto& queue : queues())
         {
            queue.moduleId.store(-1, std::memory_order_release);
            queue.readIndex.store(0, std::memory_order_relaxed);
            queue.writeIndex.store(0, std::memory_order_relaxed);
         }
      }
   } // namespace wasm
} // namespace bespoke
