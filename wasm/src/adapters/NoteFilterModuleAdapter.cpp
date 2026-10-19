/**
 * BespokeSynth WASM - Note filter (Note processor) adapter implementation
 */

#include "BespokeWasm/adapters/NoteFilterModuleAdapter.h"
#include "BespokeWasm/NoteProcessorUtil.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <cmath>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         const std::vector<SliderSpec>& noteFilterSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "low", "Low", 0.0f, 127.0f, 0.0f, SliderFormat::Integer },
               { "high", "High", 0.0f, 127.0f, 127.0f, SliderFormat::Integer },
               { "mode", "Pass", 0.0f, 1.0f, 0.0f, SliderFormat::Choice, { "Inside", "Outside" } },
            };
            return specs;
         }
      }

      bool NoteFilterModuleAdapter::passes(const NoteFilterParams& params, int pitch)
      {
         const bool inside = pitch >= params.low && pitch <= params.high;
         return inside != params.passOutside;
      }

      std::vector<WasmControlDescriptor> NoteFilterModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(noteFilterSpecs());
      }

      std::vector<PortDescriptor> NoteFilterModuleAdapter::inputPorts() const
      {
         return { { PortType::Note, "Notes" } };
      }

      std::vector<PortDescriptor> NoteFilterModuleAdapter::outputPorts() const
      {
         return { { PortType::Note, "Notes" } };
      }

      std::unique_ptr<Module> NoteFilterModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), noteFilterSpecs(), 150.0f);
      }

      void NoteFilterModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         const auto& specs = noteFilterSpecs();
         auto* params = new (dst) NoteFilterParams();
         params->low = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "low")));
         params->high = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "high")));
         params->passOutside = std::lround(sliderSpecValue(specs, controls, "mode")) == 1;
      }

      void NoteFilterModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) NoteFilterRuntimeState();
      }

      void NoteFilterModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<NoteFilterRuntimeState*>(runtimeState)->~NoteFilterRuntimeState();
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
            if (in.isNoteOn)
            {
               if (!passes(params, in.pitch))
                  continue;
               word |= bit;
               writer.push(in.pitch, in.velocity, true, in.channel);
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
