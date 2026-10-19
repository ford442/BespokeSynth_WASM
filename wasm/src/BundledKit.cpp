/**
 * BespokeSynth WASM - Bundled drum kit
 */

#include "BespokeWasm/BundledKit.h"
#include "BespokeWasm/SampleStore.h"
#include <cstdio>
#include <vector>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         // Hashes of the files scripts/gen_drum_kit.py writes (it is deterministic).
         const BundledKitSample kKit[kBundledKitSampleCount] = {
            { "kick", "b63a0733720eead60f5c26fbc78f3eb57642160bdd449389cef9da59be9e4df5", 36 },
            { "snare", "9cb47b1e1d2dd30a540f1c1ce7d856331119401a785f3e1226726bc83a3ce10b", 38 },
            { "hat", "88273e94001c742b42f7987237a6003510b1f428c88bfdf40088787b16b3801d", 42 },
            { "tom", "ca7e3978e10790f7deaa317eec3a33f00e4e279a76473571e8248b99e2d042b8", 45 },
         };

         bool readFile(const std::string& path, std::vector<uint8_t>& out)
         {
            FILE* file = std::fopen(path.c_str(), "rb");
            if (!file)
               return false;
            uint8_t chunk[4096];
            size_t got;
            while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
               out.insert(out.end(), chunk, chunk + got);
            std::fclose(file);
            return !out.empty();
         }
      }

      const BundledKitSample* bundledKitSamples()
      {
         return kKit;
      }

      const char* bundledKitDirectory()
      {
         return "/resource/kits/basic";
      }

      const SampleBuffer* findSampleOrBundled(const std::string& hash)
      {
         auto& store = SampleStore::instance();
         if (const SampleBuffer* stored = store.findByHash(hash))
            return stored;

         for (const BundledKitSample& piece : kKit)
         {
            if (hash != piece.sha256)
               continue;
            std::vector<uint8_t> bytes;
            if (!readFile(std::string(bundledKitDirectory()) + "/" + piece.name + ".wav", bytes))
               return nullptr;
            const std::string name = std::string(piece.name) + ".wav";
            if (store.loadFromMemory(bytes.data(), static_cast<int>(bytes.size()), name.c_str(), store.deviceSampleRate()) < 0)
               return nullptr;
            return store.findByHash(hash);
         }
         return nullptr;
      }

   } // namespace wasm
} // namespace bespoke
