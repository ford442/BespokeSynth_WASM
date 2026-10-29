/**
 * BespokeSynth WASM - Note router (Note processor) adapter implementation
 */

#include "BespokeWasm/adapters/NoteRouterModuleAdapter.h"
#include "BespokeWasm/NoteProcessorUtil.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "outputs", .minValue = 2.0f, .maxValue = static_cast<float>(kNoteRouterOutputs), .defaultValue = 2.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteRouterParams, outputs), .flags = kParamClamp },
            { .id = 1, .name = "mode", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteRouterParams, mode), .flags = kParamClamp },
            { .id = 2, .name = "split", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteRouterParams, split), .flags = kParamClamp },
            { .id = 3, .name = "zone", .minValue = 1.0f, .maxValue = 48.0f, .defaultValue = 12.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteRouterParams, zone), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = {
            { PortType::Note, "Out 1" },
            { PortType::Note, "Out 2" },
            { PortType::Note, "Out 3" },
            { PortType::Note, "Out 4" },
         };

         const std::vector<SliderSpec>& noteRouterSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "outputs", "Outputs", SliderFormat::Integer },
                                                                           { "mode", "Mode", SliderFormat::Choice, { "Round robin", "Pitch split" } },
                                                                           { "split", "Split", SliderFormat::Integer },
                                                                           { "zone", "Zone", SliderFormat::Integer },
                                                                           });
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

      ParamTable NoteRouterModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> NoteRouterModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> NoteRouterModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> NoteRouterModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), noteRouterSpecs(), 160.0f);
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
            if (isPitchBendEvent(in))
            {
               if (route >= 0)
                  writer.pushBend(in.pitch, in.pitchBend, in.channel, route);
               continue;
            }
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
               writer.push(in.pitch, in.velocity, true, in.channel, target, in.pitchBend);
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
