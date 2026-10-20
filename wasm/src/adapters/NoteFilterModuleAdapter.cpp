/**
 * BespokeSynth WASM - Note filter (Note processor) adapter implementation
 */

#include "BespokeWasm/adapters/NoteFilterModuleAdapter.h"
#include "BespokeWasm/NoteProcessorUtil.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "low", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 0.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteFilterParams, low), .flags = kParamClamp },
            { .id = 1, .name = "high", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 127.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteFilterParams, high), .flags = kParamClamp },
            { .id = 2, .name = "mode", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Choice1, .offset = offsetof(NoteFilterParams, passOutside), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Note, "Notes" } };

         const std::vector<SliderSpec>& noteFilterSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "low", "Low", SliderFormat::Integer },
                                                                           { "high", "High", SliderFormat::Integer },
                                                                           { "mode", "Pass", SliderFormat::Choice, { "Inside", "Outside" } },
                                                                           });
            return specs;
         }
      }

      bool NoteFilterModuleAdapter::passes(const NoteFilterParams& params, int pitch)
      {
         const bool inside = pitch >= params.low && pitch <= params.high;
         return inside != params.passOutside;
      }

      ParamTable NoteFilterModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> NoteFilterModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> NoteFilterModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> NoteFilterModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), noteFilterSpecs(), 150.0f);
      }

      void NoteFilterModuleAdapter::processNotes(void* runtimeState,
                                                 const void* paramsPtr,
                                                 const WasmNoteProcessBlock& block,
                                                 WasmNoteEvent* outNotes,
                                                 int maxNotes,
                                                 int& outCount) const
      {
         NoteWriter writer(outNotes, maxNotes, outCount);
         if (!runtimeState || !paramsPtr || !writer.valid())
            return;

         auto& state = *static_cast<NoteFilterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const NoteFilterParams*>(paramsPtr);

         for (int n = 0; n < block.noteCount; ++n)
         {
            const WasmNoteEvent& in = block.notes[n];
            if (in.pitch < 0 || in.pitch > 127)
               continue;
            uint64_t& word = state.passed[in.pitch >> 6];
            const uint64_t bit = uint64_t{ 1 } << (in.pitch & 63);
            if (isPitchBendEvent(in))
            {
               if (word & bit)
                  writer.pushBend(in.pitch, in.pitchBend, in.channel);
               continue;
            }
            if (in.isNoteOn)
            {
               if (!passes(params, in.pitch))
                  continue;
               word |= bit;
               writer.push(in.pitch, in.velocity, true, in.channel, 0, in.pitchBend);
            }
            else if (word & bit)
            {
               word &= ~bit;
               writer.push(in.pitch, 0.0f, false, in.channel);
            }
         }
      }

      BESPOKE_REGISTER_MODULE(NoteFilterModuleAdapter);

   } // namespace wasm
} // namespace bespoke
