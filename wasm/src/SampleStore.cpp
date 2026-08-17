/**
 * BespokeSynth WASM - Sample store
 */

#include "BespokeWasm/SampleStore.h"
#include <algorithm>

namespace bespoke
{
   namespace wasm
   {
      SampleStore& SampleStore::instance()
      {
         static SampleStore store;
         return store;
      }

      int SampleStore::loadFromMemory(const uint8_t* bytes, int length, const char* name, int deviceSampleRate)
      {
         if (!bytes || length <= 0)
            return -1;

         const std::string hash = sha256Hex(bytes, static_cast<size_t>(length));
         {
            std::lock_guard<std::mutex> lock(mMutex);
            auto existing = mIdByHash.find(hash);
            if (existing != mIdByHash.end())
               return existing->second;
         }

         std::vector<float> decoded;
         int sourceRate = 0;
         std::string error;
         if (!decodeAudioFile(bytes, length, decoded, sourceRate, error))
            return -1;

         const int destRate = deviceSampleRate > 0 ? deviceSampleRate : sourceRate;
         std::vector<float> resampled = resampleMonoCatmullRom(
         decoded.data(), static_cast<int>(decoded.size()), sourceRate, destRate);

         auto buffer = std::make_shared<SampleBuffer>(
         std::move(resampled), destRate, hash, name ? name : "sample");

         std::lock_guard<std::mutex> lock(mMutex);
         auto existing = mIdByHash.find(hash);
         if (existing != mIdByHash.end())
            return existing->second;

         const int id = static_cast<int>(mById.size());
         buffer->setId(id);
         mById.push_back(std::move(buffer));
         mIdByHash.emplace(hash, id);
         return id;
      }

      const SampleBuffer* SampleStore::findById(int id) const
      {
         std::lock_guard<std::mutex> lock(mMutex);
         if (id < 0 || id >= static_cast<int>(mById.size()))
            return nullptr;
         return mById[static_cast<size_t>(id)].get();
      }

      const SampleBuffer* SampleStore::findByHash(const std::string& hash) const
      {
         std::lock_guard<std::mutex> lock(mMutex);
         auto it = mIdByHash.find(hash);
         if (it == mIdByHash.end())
            return nullptr;
         return mById[static_cast<size_t>(it->second)].get();
      }

      void SampleStore::clear()
      {
         std::lock_guard<std::mutex> lock(mMutex);
         mById.clear();
         mIdByHash.clear();
      }

   } // namespace wasm
} // namespace bespoke
