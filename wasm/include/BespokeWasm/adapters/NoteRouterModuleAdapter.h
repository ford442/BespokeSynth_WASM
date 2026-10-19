/**
 * BespokeSynth WASM - Note router (Note processor) adapter
 *
 * One Note in, up to four Note outs. Round-robin deals each new note to the next
 * active output; pitch split sends notes to zones of `zone` semitones starting at
 * `split` (everything below `split` is output 1). A note-off always follows its
 * note-on to the same output, whatever the controls say by then. Events carry
 * WasmNoteEvent::outputPort, which is how the plan filters the four cables.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/WasmModuleAdapter.h"
#include <cstdint>

namespace bespoke
{
   namespace wasm
   {

      constexpr int kNoteRouterOutputs = 4;

      struct NoteRouterParams
      {
         int outputs = 2; // active outputs, 2..4
         int mode = 0; // 0 round robin, 1 pitch split
         int split = 60;
         int zone = 12;
      };

      struct NoteRouterRuntimeState
      {
         int8_t route[128]; // output a sounding pitch went to, -1 = not sounding
         int next = 0; // round-robin cursor
         NoteRouterRuntimeState()
         {
            for (int8_t& r : route)
               r = -1;
         }
      };

      class NoteRouterModuleAdapter : public WasmModuleAdapter
      {
      public:
         const char* typeId() const override { return "noterouter"; }
         const char* displayName() const override { return "Note Router"; }
         ModuleCategory category() const override { return ModuleCategory::NoteEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteProcessor; }
         bool quantizesNotesToScale() const override { return false; }

         std::vector<WasmControlDescriptor> controlDescriptors() const override;
         std::vector<PortDescriptor> inputPorts() const override;
         std::vector<PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         size_t paramsSize() const override { return sizeof(NoteRouterParams); }
         void fillParams(const WasmControlMap& controls, void* dst) const override;

         size_t runtimeStateSize() const override { return sizeof(NoteRouterRuntimeState); }
         void initRuntimeState(void* runtimeState) const override;
         void destroyRuntimeState(void* runtimeState) const override;
         void processNotes(void* runtimeState,
                           const void* params,
                           const WasmNoteProcessBlock& block,
                           WasmNoteEvent* outNotes,
                           int maxNotes,
                           int& outCount) const override;

         /** Output index for a pitch in split mode. */
         static int splitOutput(const NoteRouterParams& params, int pitch);
      };

   } // namespace wasm
} // namespace bespoke
