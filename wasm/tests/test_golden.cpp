// Golden renders: saved patches must keep sounding the same across engine refactors.
//
// Each patch in tests/golden (plus the two bundled savestates) is loaded through a ModuleCanvas,
// rendered offline and compared with the WAV recorded next to it. The WAVs keep every second
// frame of a 44.1 kHz 16-bit render (22.05 kHz, so the fixtures stay small).
// Re-record after an intended sound change: BESPOKE_UPDATE_GOLDEN=1 npm run test:unit
#include "test_common.h"
#include "BespokeWasm/InputAudioBus.h"
#include "BespokeWasm/WasmJson.h"
#include <fstream>
#include <sstream>

namespace
{
   constexpr int kGoldenRate = 44100;
   constexpr double kGoldenSeconds = 0.75;
   constexpr int kGoldenDecimation = 2;
   /** Largest per-sample difference (16-bit LSBs) still counted as the same render. */
   constexpr int kGoldenToleranceLsb = 2;

   struct GoldenPatch
   {
      const char* name;
      const char* path;
   };

   constexpr GoldenPatch kGoldenPatches[] = {
      { "starter", "/resource/userdata_original/savestate/wasm-starter.bsk" },
      { "drumkit", "/resource/userdata_original/savestate/wasm-drumkit.bsk" },
      // Re-recorded once (#125): the pre-#125 engine created runtime state lazily in processBlock, which
      // relocated this patch's reverb after block 0 and dropped that block from its tail. Nothing else moved.
      { "wave2-fx", "/golden/wave2-fx.json" },
      { "wave3-notes", "/golden/wave3-notes.json" },
      // Re-recorded once (#127): `gain` now applies its Mod input, which this patch cables from the LFO.
      { "every-dsp", "/golden/every-dsp.json" },
   };

   bool readText(const char* path, std::string& out)
   {
      std::ifstream in(path, std::ios::binary);
      if (!in)
         return false;
      std::stringstream buffer;
      buffer << in.rdbuf();
      out = buffer.str();
      return true;
   }

   // clang-format off
   bool updateRequested()
   {
      return EM_ASM_INT({
         return typeof process !== 'undefined' && process.env && process.env['BESPOKE_UPDATE_GOLDEN'] === '1' ? 1 : 0;
      }) != 0;
   }

   /** Writes to wasm/tests/golden on the host (npm runs the suite from the repo root). */
   void writeHostFile(const std::string& name, const std::vector<uint8_t>& bytes)
   {
      const std::string path = "wasm/tests/golden/" + name;
      EM_ASM({ require('fs').writeFileSync(UTF8ToString($0), HEAPU8.subarray($1, $1 + $2)); },
             path.c_str(), bytes.data(), static_cast<int>(bytes.size()));
   }
   // clang-format on

   void putU32(std::vector<uint8_t>& out, uint32_t value)
   {
      for (int i = 0; i < 4; ++i)
         out.push_back(static_cast<uint8_t>(value >> (8 * i)));
   }

   void putU16(std::vector<uint8_t>& out, uint16_t value)
   {
      out.push_back(static_cast<uint8_t>(value));
      out.push_back(static_cast<uint8_t>(value >> 8));
   }

   std::vector<int16_t> toPcm16(const std::vector<float>& interleaved)
   {
      std::vector<int16_t> pcm;
      const size_t frames = interleaved.size() / 2;
      for (size_t frame = 0; frame < frames; frame += kGoldenDecimation)
      {
         for (size_t ch = 0; ch < 2; ++ch)
         {
            const float s = std::max(-1.0f, std::min(1.0f, interleaved[frame * 2 + ch]));
            pcm.push_back(static_cast<int16_t>(std::lround(s * 32767.0f)));
         }
      }
      return pcm;
   }

   std::vector<uint8_t> encodeWav(const std::vector<int16_t>& pcm)
   {
      std::vector<uint8_t> out;
      const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2);
      out.insert(out.end(), { 'R', 'I', 'F', 'F' });
      putU32(out, 36 + dataBytes);
      out.insert(out.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
      putU32(out, 16);
      putU16(out, 1); // PCM
      putU16(out, 2);
      putU32(out, kGoldenRate / kGoldenDecimation);
      putU32(out, kGoldenRate / kGoldenDecimation * 4);
      putU16(out, 4);
      putU16(out, 16);
      out.insert(out.end(), { 'd', 'a', 't', 'a' });
      putU32(out, dataBytes);
      for (int16_t s : pcm)
         putU16(out, static_cast<uint16_t>(s));
      return out;
   }

   /** The data chunk of a WAV this file wrote (44-byte header), as samples. */
   bool decodeWav(const std::string& bytes, std::vector<int16_t>& pcm)
   {
      if (bytes.size() < 44 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(36, 4, "data") != 0)
         return false;
      pcm.resize((bytes.size() - 44) / 2);
      std::memcpy(pcm.data(), bytes.data() + 44, pcm.size() * 2);
      return true;
   }

   bool renderPatch(const std::string& json, std::vector<float>& interleaved, std::string& error,
                    double seconds = kGoldenSeconds)
   {
      ModuleCanvas::StateSnapshot loaded;
      int viewMode = 0;
      if (!deserializePatchState(json, loaded, viewMode, error))
         return false;
      ModuleCanvas canvas;
      if (!canvas.applyStateSnapshot(loaded))
      {
         error = "patch did not apply";
         return false;
      }
      canvas.setTransportPlaying(true);
      if (canvas.placeholderCount() > 0)
      {
         error = "patch names a module type this build lacks";
         return false;
      }
      return renderGraphOfflinePcm(canvas.createAudioGraphSnapshot(), seconds, kGoldenRate, interleaved, error);
   }
}

void test_golden_renders()
{
   printf("\n=== Golden Patch Renders ===\n");
   SampleStore::instance().setDeviceSampleRate(kGoldenRate);
   // Mic frames left over from the Input tests would leak into the wave-3 patch.
   std::vector<float> drain(4096);
   for (int i = 0; i < 64; ++i)
      InputAudioBus::instance().consume(drain.data(), static_cast<int>(drain.size()));

   const bool update = updateRequested();
   for (const GoldenPatch& patch : kGoldenPatches)
   {
      const std::string label = std::string("Golden '") + patch.name + "'";
      std::string json;
      std::string error;
      std::vector<float> rendered;
      if (!readText(patch.path, json) || !renderPatch(json, rendered, error))
      {
         printf("  %s: %s\n", patch.path, error.c_str());
         TEST((label + " renders").c_str(), false);
         continue;
      }
      const std::vector<int16_t> pcm = toPcm16(rendered);
      const std::string wavName = std::string(patch.name) + ".wav";
      if (update)
      {
         writeHostFile(wavName, encodeWav(pcm));
         printf("  recorded wasm/tests/golden/%s\n", wavName.c_str());
         continue;
      }

      std::string goldenBytes;
      std::vector<int16_t> golden;
      const bool haveGolden = readText(("/golden/" + wavName).c_str(), goldenBytes) && decodeWav(goldenBytes, golden);
      TEST((label + " has a recorded WAV").c_str(), haveGolden);
      if (!haveGolden)
         continue;
      int worst = 0;
      size_t differing = 0;
      size_t firstBad = pcm.size();
      for (size_t i = 0; i < std::min(golden.size(), pcm.size()); ++i)
      {
         const int diff = std::abs(static_cast<int>(golden[i]) - static_cast<int>(pcm[i]));
         worst = std::max(worst, diff);
         differing += diff > 0 ? 1 : 0;
         if (diff > kGoldenToleranceLsb && firstBad == pcm.size())
            firstBad = i;
      }
      if (worst > kGoldenToleranceLsb || golden.size() != pcm.size())
         printf("  %s: %zu of %zu samples differ, worst by %d LSB, first past tolerance at sample %zu\n", patch.name,
                differing, pcm.size(), worst, firstBad);
      TEST((label + " is not silent").c_str(), *std::max_element(pcm.begin(), pcm.end()) > 300);
      TEST((label + " sounds identical to the recording").c_str(),
           golden.size() == pcm.size() && worst <= kGoldenToleranceLsb);
   }
}

// --- Demo gallery (#127): every demo in resource-pack/demos is a golden render ---
//
// Demos render longer than the patches above (slow attacks, delay tails), so instead of a
// WAV each keeps a hash of its 16-bit render and its loudness in tests/golden/demos.json.
// The hash catches any change; the loudness tolerance says how far off a changed render is.
namespace
{
   constexpr double kDemoSeconds = 3.0;
   constexpr double kDemoLoudnessToleranceDb = 0.5;

   uint64_t fnv1a(const std::vector<int16_t>& pcm)
   {
      uint64_t hash = 1469598103934665603ull;
      for (int16_t sample : pcm)
      {
         for (int shift = 0; shift < 16; shift += 8)
         {
            hash ^= static_cast<uint8_t>(static_cast<uint16_t>(sample) >> shift);
            hash *= 1099511628211ull;
         }
      }
      return hash;
   }

   double rmsDb(const std::vector<int16_t>& pcm)
   {
      double sum = 0.0;
      for (int16_t sample : pcm)
         sum += (sample / 32767.0) * (sample / 32767.0);
      const double rms = pcm.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(pcm.size()));
      return 20.0 * std::log10(std::max(rms, 1e-9));
   }

   double stereoCorrelation(const std::vector<int16_t>& pcm)
   {
      double lr = 0.0, ll = 0.0, rr = 0.0;
      for (size_t i = 0; i + 1 < pcm.size(); i += 2)
      {
         lr += static_cast<double>(pcm[i]) * pcm[i + 1];
         ll += static_cast<double>(pcm[i]) * pcm[i];
         rr += static_cast<double>(pcm[i + 1]) * pcm[i + 1];
      }
      return ll > 0.0 && rr > 0.0 ? lr / std::sqrt(ll * rr) : 1.0;
   }

   std::string hex64(uint64_t value)
   {
      char text[17];
      snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
      return text;
   }
}

void test_golden_demos()
{
   printf("\n=== Demo Gallery Golden Renders ===\n");
   using namespace bespoke::wasm::json;
   SampleStore::instance().setDeviceSampleRate(kGoldenRate);
   std::string indexText;
   JsonValue index;
   std::string error;
   TEST("Demo index is in the resource pack", readText("/resource/demos/index.json", indexText));
   JsonParser indexParser(indexText);
   const bool indexParsed = indexParser.parse(index, error) && index.get("demos") &&
                            index.get("demos")->type == JsonValue::Type::Array;
   TEST("Demo index parses", indexParsed);
   if (!indexParsed)
      return;
   const auto& demos = index.get("demos")->arrayValue;
   TEST("Gallery has at least 8 demos", demos.size() >= 8);

   std::string goldenText;
   JsonValue golden;
   const bool update = updateRequested();
   if (!update && readText("/golden/demos.json", goldenText))
   {
      JsonParser goldenParser(goldenText);
      goldenParser.parse(golden, error);
   }

   std::string recorded = "{\n";
   for (size_t i = 0; i < demos.size(); ++i)
   {
      const std::string id = stringOr(demos[i].get("id"), "");
      const std::string label = "Demo '" + id + "'";
      std::string json;
      std::vector<float> rendered;
      if (!readText(("/resource/demos/" + stringOr(demos[i].get("file"), "")).c_str(), json) ||
          !renderPatch(json, rendered, error, kDemoSeconds))
      {
         printf("  %s: %s\n", id.c_str(), error.c_str());
         TEST((label + " loads every module and renders").c_str(), false);
         continue;
      }
      const std::vector<int16_t> pcm = toPcm16(rendered);
      const std::string hash = hex64(fnv1a(pcm));
      const double loudness = rmsDb(pcm);
      const double correlation = stereoCorrelation(pcm);
      printf("  %-16s %s  %6.2f dBFS  L/R corr %.2f\n", id.c_str(), hash.c_str(), loudness, correlation);
      char line[160];
      snprintf(line, sizeof(line), "  \"%s\": { \"hash\": \"%s\", \"rmsDb\": %.2f }%s\n", id.c_str(), hash.c_str(),
               loudness, i + 1 < demos.size() ? "," : "");
      recorded += line;
      if (update)
         continue;

      TEST((label + " is audible and not clipping flat").c_str(), loudness > -40.0 && loudness < -3.0);
      const JsonValue* expected = golden.get(id);
      TEST((label + " has a golden record").c_str(), expected != nullptr);
      if (!expected)
         continue;
      const double expectedDb = numberOr(expected->get("rmsDb"), -200.0);
      TEST((label + " loudness within 0.5 dB of the recording").c_str(),
           std::fabs(loudness - expectedDb) <= kDemoLoudnessToleranceDb);
      TEST((label + " renders identically to the recording").c_str(), stringOr(expected->get("hash"), "") == hash);
   }
   recorded += "}\n";
   if (update)
   {
      writeHostFile("demos.json", std::vector<uint8_t>(recorded.begin(), recorded.end()));
      printf("  recorded wasm/tests/golden/demos.json\n");
   }
}
