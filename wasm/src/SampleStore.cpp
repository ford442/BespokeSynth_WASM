/**
 * BespokeSynth WASM - Sample store
 */

#include "BespokeWasm/SampleStore.h"
#include <algorithm>
#include <utility>

namespace bespoke
{
   namespace wasm
   {
      SampleStore& SampleStore::instance()
      {
         static SampleStore store;
         return store;
      }

      size_t SampleStore::bytesOf(const SampleBuffer& buffer)
      {
         return static_cast<size_t>(buffer.frameCount()) * static_cast<size_t>(buffer.channelCount()) * sizeof(float);
      }

      void SampleStore::installLocked(int id, SampleBufferPtr buffer, uint32_t generation)
      {
         if (id >= static_cast<int>(mSlots.size()))
            mSlots.resize(static_cast<size_t>(id) + 1);
         Slot& slot = mSlots[static_cast<size_t>(id)];
         if (slot.buffer)
            mResidentBytes -= bytesOf(*slot.buffer);
         mResidentBytes += bytesOf(*buffer);
         slot.buffer = std::move(buffer);
         slot.generation = generation != 0 ? generation : mNextGeneration++;
         mNextGeneration = std::max(mNextGeneration, slot.generation + 1);
         // Pinned until a snapshot published after this load leaves it out.
         slot.pinned = true;
         slot.lastUse = mLastRetainGeneration;
      }

      int SampleStore::loadFromMemory(const uint8_t* bytes, int length, const char* name, int deviceSampleRate)
      {
         if (!bytes || length <= 0)
            return -1;

         const std::string hash = sha256Hex(bytes, static_cast<size_t>(length));
         {
            std::lock_guard<RtMutex> lock(mMutex);
            auto existing = mIdByHash.find(hash);
            if (existing != mIdByHash.end() && mSlots[static_cast<size_t>(existing->second)].buffer)
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

         std::lock_guard<RtMutex> lock(mMutex);
         auto existing = mIdByHash.find(hash);
         if (existing != mIdByHash.end() && mSlots[static_cast<size_t>(existing->second)].buffer)
            return existing->second;

         // An evicted hash keeps its id: patches, undo and the worklet all address it by id.
         const int id = existing != mIdByHash.end() ? existing->second : static_cast<int>(mSlots.size());
         buffer->setId(id);
         installLocked(id, std::move(buffer), 0);
         mSlots[static_cast<size_t>(id)].hash = hash;
         mIdByHash[hash] = id;
         mMissing.erase(std::remove(mMissing.begin(), mMissing.end(), hash), mMissing.end());
         return id;
      }

      const SampleBuffer* SampleStore::findById(int id) const
      {
         std::lock_guard<RtMutex> lock(mMutex);
         if (id < 0 || id >= static_cast<int>(mSlots.size()))
            return nullptr;
         return mSlots[static_cast<size_t>(id)].buffer.get();
      }

      const SampleBuffer* SampleStore::findByHash(const std::string& hash) const
      {
         std::lock_guard<RtMutex> lock(mMutex);
         auto it = mIdByHash.find(hash);
         if (it == mIdByHash.end())
            return nullptr;
         return mSlots[static_cast<size_t>(it->second)].buffer.get();
      }

      SampleHandle SampleStore::handleFor(int id) const
      {
         std::lock_guard<RtMutex> lock(mMutex);
         if (id < 0 || id >= static_cast<int>(mSlots.size()) || !mSlots[static_cast<size_t>(id)].buffer)
            return {};
         return { id, mSlots[static_cast<size_t>(id)].generation };
      }

      const SampleBuffer* SampleStore::resolve(SampleHandle handle) const
      {
         if (!handle.valid())
            return nullptr;
         std::lock_guard<RtMutex> lock(mMutex);
         if (handle.index >= static_cast<int>(mSlots.size()))
            return nullptr;
         const Slot& slot = mSlots[static_cast<size_t>(handle.index)];
         return slot.generation == handle.generation ? slot.buffer.get() : nullptr;
      }

      uint32_t SampleStore::generationOf(int id) const
      {
         std::lock_guard<RtMutex> lock(mMutex);
         if (id < 0 || id >= static_cast<int>(mSlots.size()) || !mSlots[static_cast<size_t>(id)].buffer)
            return 0;
         return mSlots[static_cast<size_t>(id)].generation;
      }

      int SampleStore::count() const
      {
         std::lock_guard<RtMutex> lock(mMutex);
         return static_cast<int>(mSlots.size());
      }

      void SampleStore::adopt(int id, std::shared_ptr<SampleBuffer> buffer, uint32_t generation)
      {
         if (id < 0 || !buffer)
            return;
         buffer->setId(id);
         std::lock_guard<RtMutex> lock(mMutex);
         if (id < static_cast<int>(mSlots.size()))
         {
            Slot& previous = mSlots[static_cast<size_t>(id)];
            if (!previous.hash.empty())
               mIdByHash.erase(previous.hash);
            previous.hash.clear();
         }
         const std::string hash = buffer->hash();
         installLocked(id, std::move(buffer), generation);
         if (!hash.empty())
         {
            mSlots[static_cast<size_t>(id)].hash = hash;
            mIdByHash[hash] = id;
         }
      }

      void SampleStore::evict(int id)
      {
         evictId(id);
      }

      void SampleStore::clear()
      {
         std::lock_guard<RtMutex> lock(mMutex);
         mSlots.clear();
         mIdByHash.clear();
         mMissing.clear();
         mResidentBytes = 0;
      }

      void SampleStore::retainOnly(std::span<const int> referencedIds, uint64_t generation)
      {
         std::lock_guard<RtMutex> lock(mMutex);
         mLastRetainGeneration = generation;
         for (size_t id = 0; id < mSlots.size(); ++id)
         {
            Slot& slot = mSlots[id];
            if (!slot.buffer)
               continue;
            const bool referenced =
            std::find(referencedIds.begin(), referencedIds.end(), static_cast<int>(id)) != referencedIds.end();
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

      int SampleStore::evictionCandidate(uint64_t safeGeneration, uint64_t& lastUse) const
      {
         std::lock_guard<RtMutex> lock(mMutex);
         int best = -1;
         for (size_t id = 0; id < mSlots.size(); ++id)
         {
            const Slot& slot = mSlots[id];
            if (!slot.buffer || slot.pinned || slot.unpinnedAt > safeGeneration)
               continue;
            if (best < 0 || slot.lastUse < mSlots[static_cast<size_t>(best)].lastUse)
               best = static_cast<int>(id);
         }
         if (best >= 0)
            lastUse = mSlots[static_cast<size_t>(best)].lastUse;
         return best;
      }

      size_t SampleStore::evictId(int id)
      {
         SampleBufferPtr released;
         size_t bytes = 0;
         {
            std::lock_guard<RtMutex> lock(mMutex);
            if (id < 0 || id >= static_cast<int>(mSlots.size()) || !mSlots[static_cast<size_t>(id)].buffer)
               return 0;
            Slot& slot = mSlots[static_cast<size_t>(id)];
            bytes = bytesOf(*slot.buffer);
            mResidentBytes -= bytes;
            released = std::move(slot.buffer);
            slot.buffer.reset();
            slot.generation = 0;
         }
         return bytes; // `released` frees the PCM outside the lock
      }

      size_t SampleStore::residentBytes() const
      {
         std::lock_guard<RtMutex> lock(mMutex);
         return mResidentBytes;
      }

      void SampleStore::noteMissing(const std::string& hash)
      {
         std::lock_guard<RtMutex> lock(mMutex);
         if (std::find(mMissing.begin(), mMissing.end(), hash) == mMissing.end())
            mMissing.push_back(hash);
      }

      std::vector<std::string> SampleStore::takeMissing()
      {
         std::lock_guard<RtMutex> lock(mMutex);
         return std::exchange(mMissing, {});
      }

      bool SampleStore::hasMissing() const
      {
         std::lock_guard<RtMutex> lock(mMutex);
         return !mMissing.empty();
      }

   } // namespace wasm
} // namespace bespoke
