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
         int channels = 1;
         int sourceRate = 0;
         std::string error;
         if (!decodeAudioFile(bytes, length, decoded, channels, sourceRate, error))
            return -1;

         // Planes are resampled independently; the hash stays over the original file bytes.
         const int destRate = deviceSampleRate > 0 ? deviceSampleRate : sourceRate;
         const int sourceFrames = static_cast<int>(decoded.size()) / channels;
         std::vector<float> resampled;
         for (int ch = 0; ch < channels; ++ch)
         {
            std::vector<float> plane = resampleMonoCatmullRom(
            decoded.data() + static_cast<size_t>(ch) * sourceFrames, sourceFrames, sourceRate, destRate);
            resampled.insert(resampled.end(), plane.begin(), plane.end());
         }

         auto buffer = std::make_shared<SampleBuffer>(
         std::move(resampled), channels, destRate, hash, name ? name : "sample");

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

      int SampleStore::count() const
      {
         std::lock_guard<std::mutex> lock(mMutex);
         return static_cast<int>(mById.size());
      }

      void SampleStore::adopt(int id, std::shared_ptr<SampleBuffer> buffer)
      {
         if (id < 0 || !buffer)
            return;
         buffer->setId(id);
         std::lock_guard<std::mutex> lock(mMutex);
         if (id >= static_cast<int>(mById.size()))
            mById.resize(static_cast<size_t>(id) + 1);
         if (const auto& previous = mById[static_cast<size_t>(id)])
            mIdByHash.erase(previous->hash());
         if (!buffer->hash().empty())
            mIdByHash[buffer->hash()] = id;
         mById[static_cast<size_t>(id)] = std::move(buffer);
      }

      void SampleStore::clear()
      {
         std::lock_guard<std::mutex> lock(mMutex);
         mById.clear();
         mIdByHash.clear();
      }

   } // namespace wasm
} // namespace bespoke
