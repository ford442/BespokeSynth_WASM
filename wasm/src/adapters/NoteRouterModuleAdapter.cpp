/**
 * BespokeSynth WASM - Note router (Note processor) adapter implementation
 */

#include "BespokeWasm/adapters/NoteRouterModuleAdapter.h"
#include "BespokeWasm/NoteProcessorUtil.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         const std::vector<SliderSpec>& noteRouterSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "outputs", "Outputs", 2.0f, static_cast<float>(kNoteRouterOutputs), 2.0f, SliderFormat::Integer },
               { "mode", "Mode", 0.0f, 1.0f, 0.0f, SliderFormat::Choice, { "Round robin", "Pitch split" } },
               { "split", "Split", 0.0f, 127.0f, 60.0f, SliderFormat::Integer },
               { "zone", "Zone", 1.0f, 48.0f, 12.0f, SliderFormat::Integer },
            };
            return specs;
         }
      }

      int NoteRouterModuleAdapter::splitOutput(const NoteRouterParams& params, int pitch)
      {
         const int outputs = std::max(2, std::min(kNoteRouterOutputs, params.outputs));
         if (pitch < params.split)
            return 0;
         const int zone = std::max(1, params.zone);
         return std::min(outputs - 1, 1 + (pitch - params.split) / zone);
      }

      std::vector<WasmControlDescriptor> NoteRouterModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(noteRouterSpecs());
      }

      std::vector<PortDescriptor> NoteRouterModuleAdapter::inputPorts() const
      {
         return { { PortType::Note, "Notes" } };
      }

      std::vector<PortDescriptor> NoteRouterModuleAdapter::outputPorts() const
      {
         return {
            { PortType::Note, "Out 1" },
            { PortType::Note, "Out 2" },
            { PortType::Note, "Out 3" },
            { PortType::Note, "Out 4" },
         };
      }

      std::unique_ptr<Module> NoteRouterModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), noteRouterSpecs(), 160.0f);
      }

      void NoteRouterModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         const auto& specs = noteRouterSpecs();
         auto* params = new (dst) NoteRouterParams();
         params->outputs = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "outputs")));
         params->mode = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "mode")));
         params->split = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "split")));
         params->zone = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "zone")));
      }

      void NoteRouterModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) NoteRouterRuntimeState();
      }

      void NoteRouterModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<NoteRouterRuntimeState*>(runtimeState)->~NoteRouterRuntimeState();
      }

      void NoteRouterModuleAdapter::processNotes(void* runtimeState,
                                                 const void* paramsPtr,
                                                 const WasmNoteProcessBlock& block,
                                                 WasmNoteEvent* outNotes,
                                                 int maxNotes,
                                                 int& outCount) const
      {
         NoteWriter writer(outNotes, maxNotes, outCount);
         if (!runtimeState || !paramsPtr || !writer.valid())
            return;

         auto& state = *static_cast<NoteRouterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const NoteRouterParams*>(paramsPtr);
         const int outputs = std::max(2, std::min(kNoteRouterOutputs, params.outputs));

         for (int n = 0; n < block.noteCount; ++n)
         {
            const WasmNoteEvent& in = block.notes[n];
            if (in.pitch < 0 || in.pitch > 127)
               continue;
            int8_t& route = state.route[in.pitch];
            if (in.isNoteOn)
            {
               if (route >= 0) // retrigger: end the old note where it went
                  writer.push(in.pitch, 0.0f, false, in.channel, route);
               int target = 0;
               if (params.mode == 1)
               {
                  target = splitOutput(params, in.pitch);
               }
               else
               {
                  target = state.next % outputs;
                  state.next = (target + 1) % outputs;
               }
               route = static_cast<int8_t>(target);
               writer.push(in.pitch, in.velocity, true, in.channel, target);
            }
            else if (route >= 0)
            {
               writer.push(in.pitch, 0.0f, false, in.channel, route);
               route = -1;
            }
         }
      }

      BESPOKE_REGISTER_MODULE(NoteRouterModuleAdapter);

   } // namespace wasm
} // namespace bespoke
