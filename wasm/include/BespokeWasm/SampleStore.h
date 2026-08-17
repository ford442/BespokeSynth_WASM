/**
 * BespokeSynth WASM - UI-thread sample store
 *
 * The audio thread only ever observes const SampleBuffer* values published
 * through an AudioGraphSnapshot. Buffers are retained for the session.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/SampleBuffer.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class SampleStore
      {
      public:
         static SampleStore& instance();

         int loadFromMemory(const uint8_t* bytes, int length, const char* name, int deviceSampleRate);
         const SampleBuffer* findById(int id) const;
         const SampleBuffer* findByHash(const std::string& hash) const;
         void clear();

      private:
         SampleStore() = default;

         mutable std::mutex mMutex;
         std::vector<SampleBufferPtr> mById;
         std::unordered_map<std::string, int> mIdByHash;
      };

   } // namespace wasm
} // namespace bespoke
