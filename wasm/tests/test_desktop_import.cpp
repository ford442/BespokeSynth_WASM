#include "test_common.h"
#include "BespokeWasm/DesktopSavestate.h"
#include "BespokeWasm/PlaceholderModule.h"
#include <fstream>
#include <sstream>

// --- Desktop savestate import (#127): .bsk layout subset, placeholders, round trip ---

namespace
{
   void appendU64(std::vector<uint8_t>& out, uint64_t value)
   {
      for (int i = 0; i < 8; ++i)
         out.push_back(static_cast<uint8_t>(value >> (8 * i)));
   }

   void appendU32(std::vector<uint8_t>& out, uint32_t value)
   {
      for (int i = 0; i < 4; ++i)
         out.push_back(static_cast<uint8_t>(value >> (8 * i)));
   }

   /** A FileStreamOut string: uint64 length (uint32 in 32-bit saves), then the bytes. */
   void appendString(std::vector<uint8_t>& out, const std::string& text, bool mode32 = false)
   {
      if (mode32)
         appendU32(out, static_cast<uint32_t>(text.size()));
      else
         appendU64(out, text.size());
      out.insert(out.end(), text.begin(), text.end());
   }

   /** New-format .bsk: "bskfile", revision, PNG bytes, layout, then (here, fake) module state. */
   std::vector<uint8_t> newFormatBsk(const std::string& layout)
   {
      std::vector<uint8_t> bytes;
      appendString(bytes, "bskfile");
      appendU32(bytes, 427);
      const std::vector<uint8_t> screenshot = { 0x89, 'P', 'N', 'G', 1, 2, 3, 4, 5 };
      appendU32(bytes, static_cast<uint32_t>(screenshot.size()));
      bytes.insert(bytes.end(), screenshot.begin(), screenshot.end());
      appendString(bytes, layout);
      for (int i = 0; i < 64; ++i)
         bytes.push_back(static_cast<uint8_t>(i * 7));
      return bytes;
   }

   // Three supported modules (+ transport/scale) and two the WASM build lacks, cabled through
   // both: notesequencer -> notewrap -> karplusstrong -> gain -> output, effectchain -> output.
   const char* const kLayout = R"({
      "modules": [
         { "name": "transport", "type": "transport", "position": [10.0, 90.0] },
         { "name": "scale", "type": "scale", "position": [160.0, 90.0] },
         { "name": "notesequencer", "type": "notesequencer", "position": [450.0, 360.0],
           "gridsteps": 16, "gridrows": 12, "target": "karplusstrong,notewrap" },
         { "name": "notewrap", "type": "notewrap", "position": [700.0, 360.0], "target": "karplusstrong" },
         { "name": "karplusstrong", "type": "karplusstrong", "position": [470.0, 660.0], "voicelimit": 4,
           "mono": false, "target": "gain" },
         { "name": "gain", "type": "gain", "position": [530.0, 830.0], "target": "output 1" },
         { "name": "my chain", "type": "effectchain", "position": [800.0, 700.0], "target": "output 1",
           "effects": [ { "type": "delay" }, { "type": "freeverb" } ] },
         { "name": "output 1", "type": "output", "position": [560.0, 920.0], "target": "" }
      ],
      "zoomlocations": {}
   })";

   std::vector<float> renderCanvas(ModuleCanvas& canvas, int blocks)
   {
      constexpr int kBlock = 512;
      canvas.setTransportPlaying(true);
      const auto graph = canvas.getAudioGraphSnapshot();
      std::vector<float> all;
      if (!graph || !graph->processPlan.valid)
         return all;
      AudioGraphSnapshot snapshot = *graph;
      AudioGraphEngine engine;
      engine.prepareForBlock(kBlock, 128, 32);
      engine.prepareRuntimeStates(snapshot);
      std::vector<float> left(kBlock);
      std::vector<float> right(kBlock);
      for (int b = 0; b < blocks; ++b)
      {
         float* outputs[2] = { left.data(), right.data() };
         gAudioCallbackActive.store(true, std::memory_order_release);
         engine.processBlock(snapshot, outputs, 2, kBlock, 44100.0f);
         gAudioCallbackActive.store(false, std::memory_order_release);
         all.insert(all.end(), left.begin(), left.end());
      }
      return all;
   }

   std::vector<std::string> placeholderNames(ModuleCanvas& canvas)
   {
      std::vector<std::string> names;
      for (const auto& module : canvas.createStateSnapshot().modules)
      {
         Module* live = canvas.getModule(module.id);
         if (live && live->isPlaceholder())
            names.push_back(live->getName());
      }
      std::sort(names.begin(), names.end());
      return names;
   }
}

void test_desktop_import_header()
{
   printf("\n=== Desktop import: .bsk header ===\n");
   TEST("Desktop types map onto WASM adapters",
        std::string(wasmTypeForDesktopType("drumsequencer")) == "drumseq" &&
        std::string(wasmTypeForDesktopType("biquad")) == "filter" &&
        std::string(wasmTypeForDesktopType("fftvocoder")) == "vocoder" &&
        wasmTypeForDesktopType("effectchain") == nullptr && wasmTypeForDesktopType("lfo") == nullptr);

   std::string json;
   std::string error;
   int rev = -1;
   std::vector<uint8_t> oldFormat;
   appendString(oldFormat, kLayout);
   appendU32(oldFormat, 12345); // module state follows the layout
   TEST("Old .bsk (layout first) is recognized", looksLikeDesktopSavestate(oldFormat.data(), oldFormat.size()));
   TEST("Old .bsk yields its layout",
        extractDesktopLayoutJson(oldFormat.data(), oldFormat.size(), json, rev, error) && json == kLayout && rev == 0);

   const auto newFormat = newFormatBsk(kLayout);
   TEST("New .bsk skips the header and screenshot",
        extractDesktopLayoutJson(newFormat.data(), newFormat.size(), json, rev, error) && json == kLayout &&
        rev == 427);

   std::vector<uint8_t> old32;
   appendString(old32, kLayout, true);
   // A 32-bit save's first uint64 "length" spans the layout's first bytes: absurdly large.
   TEST("32-bit .bsk is read with uint32 lengths",
        extractDesktopLayoutJson(old32.data(), old32.size(), json, rev, error) && json == kLayout);

   std::vector<uint8_t> truncated(newFormat.begin(), newFormat.begin() + 40);
   TEST("Truncated .bsk fails with a reason",
        !extractDesktopLayoutJson(truncated.data(), truncated.size(), json, rev, error) && !error.empty());
   const std::string wasmPatch = "{\"filetype\": \"bespokesynth\"}";
   TEST("WASM JSON patches are not mistaken for desktop saves",
        !looksLikeDesktopSavestate(reinterpret_cast<const uint8_t*>(wasmPatch.data()), wasmPatch.size()));
}

void test_desktop_import_layout()
{
   printf("\n=== Desktop import: layout to canvas ===\n");
   const auto bytes = newFormatBsk(kLayout);
   ModuleCanvas::StateSnapshot snapshot;
   DesktopImportReport report;
   std::string error;
   TEST("Desktop savestate imports", importDesktopSavestate(bytes.data(), bytes.size(), snapshot, report, error));
   TEST("Import reads every module, two of them placeholders", report.modules == 8 && report.placeholders == 2);
   TEST("Import keeps all six cables, none dropped", report.connections == 6 && report.droppedTargets == 0);

   const ModuleCanvas::StateModule* ks = nullptr;
   const ModuleCanvas::StateModule* seq = nullptr;
   const ModuleCanvas::StateModule* chain = nullptr;
   for (const auto& module : snapshot.modules)
   {
      if (module.type == "karplusstrong")
         ks = &module;
      if (module.type == "notesequencer")
         seq = &module;
      if (module.type == "effectchain")
         chain = &module;
   }
   TEST("voicelimit aliases to voices", ks && ks->controls.count("voices") && ks->controls.at("voices") == 4.0f);
   TEST("Grid size aliases to steps and rows",
        seq && seq->controls.at("steps") == 16.0f && seq->controls.at("rows") == 12.0f);
   TEST("Placeholders keep the desktop name and position",
        chain && chain->extras.count("label") && chain->extras.at("label") == "my chain" && chain->x == 800.0f &&
        chain->y == 700.0f);

   ModuleCanvas canvas;
   TEST("Imported layout applies to the canvas", canvas.applyStateSnapshot(snapshot));
   TEST("Canvas shows both placeholders", canvas.placeholderCount() == 2);
   const auto names = placeholderNames(canvas);
   TEST("Placeholders are titled with the desktop names",
        names == std::vector<std::string>({ "my chain", "notewrap" }));

   const auto audio = renderCanvas(canvas, 120);
   TEST("Imported patch plays: sequencer -> karplus -> gain -> output", buffer_rms(audio) > 0.002f && all_finite(audio));

   // Save, load, save again: the unsupported nodes and their cables survive.
   const std::string saved = serializePatchState(canvas.createStateSnapshot(), 0);
   ModuleCanvas::StateSnapshot reread;
   int viewMode = 0;
   TEST("Re-saved patch parses", deserializePatchState(saved, reread, viewMode, error));
   int notewrap = 0;
   int effectchain = 0;
   for (const auto& module : reread.modules)
   {
      notewrap += module.type == "notewrap" ? 1 : 0;
      effectchain += module.type == "effectchain" ? 1 : 0;
   }
   TEST("Re-saved patch keeps the unsupported modules", notewrap == 1 && effectchain == 1);
   TEST("Re-saved patch keeps every cable", reread.connections.size() == snapshot.connections.size());

   ModuleCanvas reloaded;
   TEST("Re-saved patch reloads with its placeholders",
        reloaded.applyStateSnapshot(reread) && reloaded.placeholderCount() == 2 &&
        placeholderNames(reloaded) == names);
   ModuleCanvas::StateSnapshot second;
   TEST("Second save is stable",
        deserializePatchState(serializePatchState(reloaded.createStateSnapshot(), 0), second, viewMode, error) &&
        second.modules.size() == reread.modules.size() && second.connections.size() == reread.connections.size());
}

void test_desktop_import_cables_and_collisions()
{
   printf("\n=== Desktop import: cable ports, aliases, name collisions ===\n");
   const std::string layout = R"({ "modules": [
      { "name": "osc", "type": "oscillator", "position": [0, 0], "osc": 3, "vol": 0.25, "target": "send" },
      { "name": "send", "type": "send", "position": [0, 100], "target": "output", "target2": "verb" },
      { "name": "verb", "type": "freeverb", "position": [200, 100], "target": "output" },
      { "name": "env", "type": "envelope", "position": [200, 0], "target": "verb~roomsize" },
      { "name": "lfo", "type": "lfo", "position": [400, 0], "target": "osc~vol" },
      { "name": "output", "type": "output", "position": [0, 200], "target": "missing" }
   ] })";
   ModuleCanvas::StateSnapshot snapshot;
   DesktopImportReport report;
   std::string error;
   TEST("Second layout imports",
        importDesktopSavestate(reinterpret_cast<const uint8_t*>(layout.data()), layout.size(), snapshot, report,
                               error));
   auto byType = [&](const std::string& type) -> const ModuleCanvas::StateModule*
   {
      for (const auto& module : snapshot.modules)
      {
         if (module.type == type)
            return &module;
      }
      return nullptr;
   };
   const auto* osc = byType("oscillator");
   TEST("Desktop oscillator type and volume alias",
        osc && osc->controls.at("waveform") == 1.0f && std::fabs(osc->controls.at("volume") - 0.25f) < 1e-6f);
   TEST("A desktop type that is also a WASM id loads as a prefixed placeholder",
        byType("desktop:lfo") != nullptr && byType("lfo") == nullptr);

   const auto* send = byType("send");
   const auto* verb = byType("reverb");
   bool sendAux = false;
   bool envelopeToReverb = false;
   for (const auto& connection : snapshot.connections)
   {
      sendAux |= send && verb && connection.sourceModuleId == send->id && connection.sourcePortIndex == 1 &&
                 connection.destModuleId == verb->id;
      envelopeToReverb |= verb && connection.destModuleId == verb->id && connection.sourceModuleId == byType("envelope")->id;
   }
   TEST("target2 is the second output (send -> reverb from Send)", sendAux);
   // The reverb has no modulation input: a control target with nowhere to land is dropped,
   // as is a target naming no module.
   TEST("Unconnectable and unknown targets are dropped", !envelopeToReverb && report.droppedTargets == 2);

   ModuleCanvas canvas;
   TEST("Second layout applies with one placeholder",
        canvas.applyStateSnapshot(snapshot) && canvas.placeholderCount() == 1);
}

void test_desktop_import_bundled_example()
{
   printf("\n=== Desktop import: bundled desktop example ===\n");
   std::ifstream file("/desktop/example__sequencing_sequencers.bsk", std::ios::binary);
   std::ostringstream contents;
   contents << file.rdbuf();
   const std::string bytes = contents.str();
   TEST("Desktop example is embedded", bytes.size() > 1000);
   ModuleCanvas::StateSnapshot snapshot;
   DesktopImportReport report;
   std::string error;
   TEST("Desktop example imports",
        importDesktopSavestate(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), snapshot, report,
                               error));
   printf("  %d modules, %d placeholders, %d cables, %d targets dropped\n", report.modules, report.placeholders,
          report.connections, report.droppedTargets);
   // Two comments; notesequencer2 -> notesequencer (no note input in the WASM sequencer) drops.
   TEST("Desktop example: 9 modules, the 2 comments as placeholders", report.modules == 9 && report.placeholders == 2);
   ModuleCanvas canvas;
   TEST("Desktop example applies", canvas.applyStateSnapshot(snapshot) && canvas.placeholderCount() == 2);
   const auto audio = renderCanvas(canvas, 120);
   TEST("Desktop example plays", buffer_rms(audio) > 0.002f && all_finite(audio));
}
