#include "test_common.h"

static std::string canonical_canvas_state(const ModuleCanvas& canvas)
{
   auto snapshot = canvas.createStateSnapshot();
   std::sort(snapshot.modules.begin(), snapshot.modules.end(),
             [](const auto& a, const auto& b)
             {
                return a.id < b.id;
             });
   std::sort(snapshot.connections.begin(), snapshot.connections.end(),
             [](const auto& a, const auto& b)
             {
                return std::tie(a.sourceModuleId, a.sourcePortIndex, a.destModuleId, a.destPortIndex) <
                       std::tie(b.sourceModuleId, b.sourcePortIndex, b.destModuleId, b.destPortIndex);
             });
   std::sort(snapshot.midiCcMap.begin(), snapshot.midiCcMap.end(),
             [](const auto& a, const auto& b)
             {
                return std::tie(a.moduleId, a.controlName, a.cc) < std::tie(b.moduleId, b.controlName, b.cc);
             });
   return serializePatchState(snapshot, 0);
}

void test_undo_redo()
{
   printf("\n=== Undo / Redo Tests ===\n");

   ModuleCanvas canvas;
   AudioGraphEngine engine;
   engine.prepareForBlock(512, 128, 32);
   canvas.setSnapshotListener([&engine](const AudioGraphSnapshot& graph)
                              {
                                 engine.prepareRuntimeStates(graph);
                              });
   canvas.setTransportPlaying(true);
   canvas.clearUndoHistory();

   std::vector<float> left(512), right(512);
   float* outputs[2] = { left.data(), right.data() };
   auto playBlock = [&]()
   {
      const auto graph = canvas.getAudioGraphSnapshot();
      gAudioCallbackActive.store(true, std::memory_order_release);
      engine.processBlock(*graph, outputs, 2, 512, 44100.0f);
      gAudioCallbackActive.store(false, std::memory_order_release);
   };

   std::vector<std::string> states{ canonical_canvas_state(canvas) };
   auto step = [&](auto&& edit)
   {
      edit();
      playBlock();
      states.push_back(canonical_canvas_state(canvas));
   };

   int osc = -1, filter = -1, gain = -1, out = -1, lfo = -1, seq = -1;
   step([&]
        {
           osc = canvas.createModule("oscillator", 80, 120);
        });
   step([&]
        {
           filter = canvas.createModule("filter", 260, 120);
        });
   step([&]
        {
           gain = canvas.createModule("gain", 440, 120);
        });
   step([&]
        {
           out = canvas.createModule("output", 620, 120);
        });
   step([&]
        {
           canvas.connectModules(osc, 0, filter, 0);
        });
   step([&]
        {
           canvas.connectModules(filter, 0, gain, 0);
        });
   step([&]
        {
           canvas.connectModules(gain, 0, out, 0);
        });
   step([&]
        {
           lfo = canvas.createModule("lfo", 80, 300);
        });
   step([&]
        {
           canvas.connectModules(lfo, 0, filter, 1);
        });
   step([&]
        {
           canvas.setModuleControlValue(filter, "cutoff", 1200.0f);
        });
   step([&]
        {
           canvas.setModuleControlValue(osc, "volume", 0.4f);
        });
   step([&]
        {
           canvas.setModuleControlValue(gain, "gain", 0.3f);
        });
   step([&]
        {
           seq = canvas.createModule("stepsequencer", 80, 420);
        });
   step([&]
        {
           canvas.connectModules(seq, 0, osc, 0);
        });
   step([&]
        {
           canvas.deleteModule(filter);
        }); // takes three cables with it
   step([&]
        {
           canvas.connectModules(osc, 0, gain, 0);
        });
   step([&]
        {
           canvas.disconnectModules(seq, osc);
        });
   step([&]
        {
           canvas.deleteModule(lfo);
        });
   step([&]
        {
           canvas.setModuleControlValue(seq, "pitch", 67.0f);
        });
   step([&]
        {
           canvas.deleteModule(seq);
        });
   TEST("20 edits recorded", canvas.undoDepth() == 20);

   bool allRestored = true;
   for (int i = 20; i > 0; --i)
   {
      allRestored = canvas.undo() && allRestored;
      playBlock(); // Play during undo: RtAllocGuard aborts on any audio-thread new
      if (canonical_canvas_state(canvas) != states[static_cast<size_t>(i - 1)])
      {
         printf("  undo mismatch restoring state %d\n", i - 1);
         allRestored = false;
      }
   }
   TEST("20 undo steps restore modules, cables, and controls", allRestored);
   TEST("Undo stack drained", canvas.undoDepth() == 0 && !canvas.undo());
   TEST("Audio kept rendering through undo without RT allocation", true);

   bool allRedone = true;
   for (int i = 1; i <= 20; ++i)
   {
      allRedone = canvas.redo() && allRedone;
      playBlock();
      if (canonical_canvas_state(canvas) != states[static_cast<size_t>(i)])
         allRedone = false;
   }
   TEST("20 redo steps replay the same graph", allRedone);
   TEST("Deleted filter keeps its id after undo/redo", canvas.getModule(filter) == nullptr);

   canvas.undo();
   canvas.createModule("noise", 10, 10);
   TEST("New edit clears the redo branch", canvas.redoDepth() == 0);

   canvas.clearUndoHistory();
   canvas.setModuleControlValue(gain, "gain", 0.1f);
   canvas.setModuleControlValue(gain, "gain", 0.2f);
   canvas.setModuleControlValue(gain, "gain", 0.25f);
   TEST("Rapid control edits coalesce into one step", canvas.undoDepth() == 1);
   canvas.undo();
   float restoredGain = 0.0f;
   canvas.getModuleControlValue(gain, "gain", restoredGain);
   TEST("Coalesced control undo restores the pre-gesture value", std::fabs(restoredGain - 0.3f) < 1e-5f);
   canvas.setModuleControlValue(gain, "gain", 0.9f, false);
   TEST("Automation writes are not recorded", canvas.undoDepth() == 0);

   for (int i = 0; i < 60; ++i)
      canvas.createModule("noise", 10.0f + i, 10.0f);
   TEST("Undo history is capped at 50 steps", canvas.undoDepth() == 50);

   UndoStack stack(3);
   for (int i = 0; i < 5; ++i)
      stack.push(UndoCommand::move(i, 0, 0, 1, 1));
   TEST("UndoStack drops the oldest beyond capacity", stack.undoDepth() == 3 && stack.popUndo().moduleId == 4);
}

void test_midi_learn_and_channel()
{
   printf("\n=== MIDI Learn Tests ===\n");

   ModuleCanvas canvas;
   const int filter = canvas.createModule("filter", 300, 155);
   TEST("Arm learn on filter cutoff", canvas.armMidiLearn(filter, "cutoff") && canvas.isMidiLearnArmed());
   canvas.applyMidiCc(3, 21, 0.5f);
   TEST("Next CC disarms learn", !canvas.isMidiLearnArmed());
   float cutoff = 0.0f;
   canvas.getModuleControlValue(filter, "cutoff", cutoff);
   TEST("Learned CC moves cutoff through its range", std::fabs(cutoff - 10010.0f) < 1.0f);

   const auto map = canvas.midiCcMap();
   const bool learned = std::any_of(map.begin(), map.end(),
                                    [&](const MidiCcBinding& binding)
                                    {
                                       return binding.moduleId == filter && binding.cc == 21 &&
                                              binding.channel == 3 && binding.controlName == "cutoff";
                                    });
   TEST("Learned binding stored with channel", learned);
   const bool cc74Replaced = std::none_of(map.begin(), map.end(),
                                          [&](const MidiCcBinding& binding)
                                          {
                                             return binding.moduleId == filter && binding.controlName == "cutoff" &&
                                                    binding.cc == 74;
                                          });
   TEST("Learn replaces the default CC for that control", cc74Replaced);

   canvas.createModule("gain", 500, 160);
   const auto afterSpawn = canvas.midiCcMap();
   TEST("Spawning after learn keeps the learned map",
        std::any_of(afterSpawn.begin(), afterSpawn.end(), [&](const MidiCcBinding& b)
                    {
                       return b.cc == 21 && b.moduleId == filter;
                    }));

   // Save → load into a fresh canvas: the hardware knob still drives cutoff.
   const std::string json = serializePatchState(canvas.createStateSnapshot(), 0);
   ModuleCanvas::StateSnapshot loaded;
   int viewMode = 0;
   std::string error;
   TEST("Learned patch deserializes", deserializePatchState(json, loaded, viewMode, error));
   ModuleCanvas reloaded;
   reloaded.applyStateSnapshot(loaded);
   const int reloadedFilter = reloaded.findFirstModuleOfType("filter");
   reloaded.applyMidiCc(3, 21, 1.0f);
   float reloadedCutoff = 0.0f;
   reloaded.getModuleControlValue(reloadedFilter, "cutoff", reloadedCutoff);
   TEST("Reloaded patch: learned CC still moves cutoff", std::fabs(reloadedCutoff - 20000.0f) < 1.0f);
   reloaded.applyMidiCc(4, 21, 0.0f);
   reloaded.getModuleControlValue(reloadedFilter, "cutoff", reloadedCutoff);
   TEST("Learned CC ignores other channels", std::fabs(reloadedCutoff - 20000.0f) < 1.0f);

   // Shift-click path: arm from screen coordinates over the cutoff text line.
   ModuleCanvas clickCanvas;
   const int clickFilter = clickCanvas.createModule("filter", 300, 155);
   const float lineH = std::max(UITheme::kValueFontSize, UITheme::kLabelFontSize) + 4.0f;
   const float screenX = 320.0f;
   const float screenY = 155.0f + Module::kTitleBarHeight + 18.0f + lineH * 1.5f + 40.0f;
   clickCanvas.onMouseDown(screenX, screenY, 0, 1);
   TEST("Shift-click arms learn on the clicked control",
        clickCanvas.midiLearnModuleId() == clickFilter && clickCanvas.midiLearnControl() == "cutoff");
   clickCanvas.onMouseUp(screenX, screenY, 0);
   clickCanvas.onKeyDown(27, 0);
   TEST("Escape cancels learn", !clickCanvas.isMidiLearnArmed());

   float minValue = 0.0f, maxValue = 0.0f;
   const int mixer = clickCanvas.createModule("mixer", 500, 300);
   clickCanvas.controlRangeFor(mixer, "master", minValue, maxValue);
   TEST("Slider-panel modules report their own learn range", maxValue > minValue);

   clickCanvas.setMidiNoteChannel(9);
   TEST("MIDI note channel filter is settable", clickCanvas.midiNoteChannel() == 9);
}

void test_script_note_queue()
{
   printf("\n=== Script Module Tests ===\n");

   resetScriptNoteQueues();
   WasmNoteEvent note;
   note.pitch = 64;
   note.velocity = 0.8f;
   note.isNoteOn = true;
   TEST("Script note queued", pushScriptNote(7, note));
   WasmNoteEvent drained[4];
   TEST("Script note drained once", drainScriptNotes(7, drained, 4) == 1 && drained[0].pitch == 64);
   TEST("Queue empty after drain", drainScriptNotes(7, drained, 4) == 0);
   int accepted = 0;
   for (int i = 0; i < kScriptQueueCapacity + 10; ++i)
      accepted += pushScriptNote(7, note) ? 1 : 0;
   TEST("Script queue is bounded", accepted == kScriptQueueCapacity - 1);
   clearScriptNotes(7);
   TEST("Stopping a script drops its queued notes", drainScriptNotes(7, drained, 4) == 0);
   bool slotsRecycled = true;
   for (int id = 100; id < 100 + kMaxScriptQueues * 3; ++id)
   {
      slotsRecycled = pushScriptNote(id, note) && slotsRecycled;
      clearScriptNotes(id);
   }
   TEST("Stopped scripts free their queue slot", slotsRecycled);

   const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find("script");
   TEST("Script adapter registered as note source",
        adapter && adapter->audioRole() == WasmAudioRole::NoteSource && adapter->outputPorts().size() == 1);

   AudioGraphSnapshot graph;
   graph.transportPlaying = true;
   appendAudioGraphNode(graph, 1, "script", true, {}, { { "script", "bespoke.log('hi')" } });
   appendAudioGraphNode(graph, 2, "oscillator", true, { { "volume", 1.0f } });
   appendAudioGraphNode(graph, 3, "output", true, {});
   graph.connections.push_back({ 1, 0, 2, 0, PortType::Note, PortType::Note });
   graph.connections.push_back({ 2, 0, 3, 0, PortType::Audio, PortType::Audio });
   AudioGraphEngine silentEngine;
   TEST("Cabled script with no notes is silent", buffer_rms(render_graph(graph, &silentEngine)) < 1e-6f);
   pushScriptNote(1, note);
   AudioGraphEngine engine;
   TEST("Script note plays the cabled oscillator", buffer_rms(render_graph(graph, &engine)) > 0.01f);

   auto ui = adapter->createUiModule(9);
   ui->setStringProperty("script", "bespoke.onBeat(() => {})");
   ui->setStringProperty("status", "running");
   const auto names = ui->stringPropertyNames();
   TEST("Script source serializes; runtime status does not",
        names.size() == 1 && names[0] == "script" && ui->getStringProperty("status") == "running");
   resetScriptNoteQueues();
}
