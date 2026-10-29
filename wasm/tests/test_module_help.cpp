#include "test_common.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/ModuleHelp.h"
#include "RecordingRenderer.h"
#include <cctype>
#include <set>

// --- Module help (#127): baked tooltips, spawn-menu descriptions, hover help ---

namespace
{
   const char* const kHelpWave4Types[] = { "fmsynth", "karplusstrong", "seaofgrain", "notechance", "capo",
                                           "pitchbender", "notesequencer", "bitcrush", "distortion", "stereowidth",
                                           "pingpongdelay", "vocoder", "send", "envelope", "ramper" };

   bool isWave4(const std::string& type)
   {
      for (const char* wave4 : kHelpWave4Types)
      {
         if (type == wave4)
            return true;
      }
      return false;
   }

   /** Control names the pointer can reach on a module's UI (2 px scan of its body). */
   std::set<std::string> hoverableControls(const Module& module)
   {
      std::set<std::string> names;
      for (float y = module.getY() + Module::kTitleBarHeight; y < module.getY() + module.getHeight(); y += 2.0f)
      {
         for (float x = module.getX(); x < module.getX() + module.getWidth(); x += 2.0f)
         {
            std::string name = module.controlNameAt(x, y);
            if (!name.empty())
               names.insert(std::move(name));
         }
      }
      return names;
   }
}

void test_module_help_table()
{
   printf("\n=== Module help: baked table ===\n");
   ModuleHelp& help = ModuleHelp::instance();
   help.setLocale("en");
   TEST("Help table loads from the resource pack", help.typeCount() >= 40);

   int wave4Missing = 0;
   int otherControls = 0;
   int otherCovered = 0;
   for (const auto& info : WasmModuleAdapterRegistry::instance().registeredTypes())
   {
      const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find(info.type);
      const bool wave4 = isWave4(info.type);
      if (wave4 && help.description(info.type).empty())
         ++wave4Missing;
      for (const ParamDescriptor& param : adapter->paramTable())
      {
         const bool covered = !help.controlTooltip(info.type, param.name).empty();
         if (wave4)
            wave4Missing += covered ? 0 : 1;
         else
         {
            ++otherControls;
            otherCovered += covered ? 1 : 0;
         }
      }
   }
   TEST("Every wave-4 module has a description and every control a tooltip", wave4Missing == 0);
   TEST("At least 80% of the other modules' controls have a tooltip",
        otherControls > 0 && otherCovered * 5 >= otherControls * 4);

   help.setLocale("de-DE");
   TEST("Locale tag keeps the language subtag", help.locale() == "de");
   const std::string german = help.description("fmsynth");
   help.setLocale("en-US");
   const std::string english = help.description("fmsynth");
   TEST("German description comes from tooltips_deu.txt", !german.empty() && german != english);
   help.setLocale("de");
   TEST("A control the translation lacks falls back to English",
        help.controlTooltip("fmsynth", "voices") == "maximum number of notes that sound at once");
   help.setLocale("xx");
   TEST("An unknown locale falls back to English", help.description("fmsynth") == english);
   help.setLocale("en");
   TEST("Unknown types and controls have no help",
        help.description("nosuchmodule").empty() && help.controlTooltip("fmsynth", "nosuchcontrol").empty());
}

void test_module_help_wrap_text()
{
   printf("\n=== Module help: word wrap ===\n");
   RecordingRenderer renderer;
   renderer.fontSize(10.0f); // the recorder's glyphs are 5 px wide at 10 px
   const auto lines = wrapText(renderer, "one two three four", 50.0f);
   TEST("Words wrap at the width", lines.size() == 2 && lines[0] == "one two" && lines[1] == "three four");
   const auto cut = wrapText(renderer, "one two three four five six", 50.0f, 2);
   TEST("A line limit ends the last line in an ellipsis",
        cut.size() == 2 && cut[1].size() >= 3 && cut[1].compare(cut[1].size() - 3, 3, "...") == 0 &&
        renderer.textWidth(cut[1].c_str()) <= 50.0f);
   const auto paragraphs = wrapText(renderer, "first\nsecond", 200.0f);
   TEST("Newlines start a new line", paragraphs.size() == 2 && paragraphs[1] == "second");
}

void test_module_help_canvas()
{
   printf("\n=== Module help: spawn menu and hover ===\n");
   ModuleHelp::instance().setLocale("en");
   ModuleCanvas canvas;
   canvas.openSpawnMenu(100.0f, 120.0f);
   for (char c : std::string("fmsynth"))
      canvas.onKeyDown(static_cast<int>(std::toupper(static_cast<unsigned char>(c))), 0);
   TEST("Spawn menu shows the selected module's description",
        canvas.spawnMenuDescription() == ModuleHelp::instance().description("fmsynth"));
   RecordingRenderer menuRenderer;
   canvas.render(menuRenderer, 1280, 720);
   bool drewDescription = false;
   const std::string firstWord = "polyphonic";
   for (const auto& call : menuRenderer.calls)
      drewDescription |= call.op == "text" && call.text.rfind(firstWord, 0) == 0;
   TEST("Spawn menu draws the description under the rows", drewDescription);
   canvas.closeSpawnMenu();
   TEST("Closed spawn menu has no description", canvas.spawnMenuDescription().empty());

   // Screen = world + (0, canvas title bar) at the default view.
   const float screenTop = 40.0f; // ModuleCanvas title bar
   const int fm = canvas.createModule("fmsynth", 300.0f, 100.0f);
   Module* fmModule = canvas.getModule(fm);
   const auto fmControls = hoverableControls(*fmModule);
   TEST("Every FM synth control is reachable by the pointer", fmControls.size() == WasmModuleAdapterRegistry::instance().find("fmsynth")->paramTable().size());

   bool found = false;
   for (float y = 100.0f + Module::kTitleBarHeight; y < 100.0f + fmModule->getHeight() && !found; y += 2.0f)
   {
      if (fmModule->controlNameAt(310.0f, y) != "voices")
         continue;
      canvas.onMouseMove(310.0f, y + screenTop, 310.0f, y + screenTop);
      found = true;
   }
   TEST("Hovering a control shows its tooltip",
        found && canvas.hoverHelpText() == ModuleHelp::instance().controlTooltip("fmsynth", "voices"));

   canvas.onMouseMove(310.0f, 100.0f + 6.0f + screenTop, 310.0f, 100.0f + 6.0f + screenTop);
   TEST("Hovering the title bar shows the module description",
        canvas.hoverHelpText() == ModuleHelp::instance().description("fmsynth"));

   RecordingRenderer hoverRenderer;
   bool drewTooltip = false;
   for (int frame = 0; frame < 30 && !drewTooltip; ++frame)
   {
      hoverRenderer.calls.clear();
      canvas.render(hoverRenderer, 1280, 720);
      for (const auto& call : hoverRenderer.calls)
         drewTooltip |= call.op == "text" && call.text.rfind(firstWord, 0) == 0;
   }
   TEST("The canvas draws the tooltip after a short dwell", drewTooltip);

   canvas.onMouseDown(310.0f, 100.0f + 6.0f + screenTop, 0);
   TEST("A click dismisses the tooltip", canvas.hoverHelpText().empty());
   canvas.onMouseUp(310.0f, 100.0f + 6.0f + screenTop, 0);

   canvas.onMouseMove(1200.0f, 700.0f, 1200.0f, 700.0f);
   TEST("Empty canvas has no tooltip", canvas.hoverHelpText().empty());

   // Hover reach over the whole catalogue: wave 4 complete, >= 80% of the rest.
   int wave4Unreachable = 0;
   int otherControls = 0;
   int otherHoverable = 0;
   float x = 0.0f;
   for (const auto& info : WasmModuleAdapterRegistry::instance().registeredTypes())
   {
      if (info.type == "transport" || info.type == "scale" || info.type == "script")
         continue;
      const int id = canvas.createModule(info.type, 2000.0f + x, 2000.0f);
      x += 400.0f;
      Module* module = canvas.getModule(id);
      const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find(info.type);
      if (!module || !adapter)
         continue;
      if (info.type == "notesequencer")
         module->setControlValue("steps", 16.0f); // per-step cells exist only for the steps in use
      const auto reachable = hoverableControls(*module);
      for (const ParamDescriptor& param : adapter->paramTable())
      {
         const bool ok = reachable.count(param.name) &&
                         !ModuleHelp::instance().controlTooltip(info.type, param.name).empty();
         if (isWave4(info.type))
         {
            if (!ok)
            {
               printf("  unreachable wave-4 control: %s.%s\n", info.type.c_str(), param.name);
               ++wave4Unreachable;
            }
         }
         else
         {
            ++otherControls;
            otherHoverable += ok ? 1 : 0;
         }
      }
   }
   printf("  hover help reaches %d of %d wave-1/2 controls\n", otherHoverable, otherControls);
   TEST("Every wave-4 control shows hover help", wave4Unreachable == 0);
   TEST("Hover help reaches at least 80% of wave-1/2 controls", otherHoverable * 5 >= otherControls * 4);
}
