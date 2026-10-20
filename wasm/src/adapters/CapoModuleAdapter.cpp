/**
 * BespokeSynth WASM - Capo adapter implementation
 */

#include "BespokeWasm/adapters/CapoModuleAdapter.h"
#include "BespokeWasm/ScaleQuantize.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "capo", .minValue = -24.0f, .maxValue = 24.0f, .defaultValue = 0.0f, .kind = ParamKind::IntRound, .offset = offsetof(CapoParams, capo), .flags = kParamClamp },
            { .id = 1, .name = "retrigger", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Bool, .offset = offsetof(CapoParams, retrigger) },
            { .id = 2, .name = "diatonic", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Bool, .offset = offsetof(CapoParams, diatonic) },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Note, "Out" } };

         const std::vector<SliderSpec>& capoSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "capo", "Capo", SliderFormat::Integer },
                                                                           { "retrigger", "Retrigger", SliderFormat::Choice, { "off", "on" } },
                                                                           { "diatonic", "Diatonic", SliderFormat::Choice, { "off", "on" } },
                                                                           });
            return specs;
         }
      }

      int CapoModuleAdapter::shifted(int pitch, const CapoParams& params, const GraphMusicScale* scale)
      {
         if (params.diatonic && scale)
            return transposeByScaleDegrees(pitch, params.capo, *scale);
         return clampMidiPitch(pitch + params.capo);
      }

      ParamTable CapoModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> CapoModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> CapoModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> CapoModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), capoSpecs(), 140.0f);
      }

      bool CapoModuleAdapter::transformNoteOn(CapoExtraState& extra,
                                              const CapoParams& params,
                                              const WasmNoteProcessBlock& block,
                                              const WasmNoteEvent& in,
                                              int& pitch,
                                              float& velocity) const
      {
         (void)extra;
         (void)velocity;
         pitch = shifted(in.pitch, params, block.scale);
         return true;
      }

      void CapoModuleAdapter::beginBlock(CapoExtraState& extra,
                                         const CapoParams& params,
                                         const WasmNoteProcessBlock& block,
                                         NoteTransformHeld& held,
                                         NoteWriter& writer) const
      {
         const bool changed = extra.appliedCapo != params.capo || extra.appliedDiatonic != params.diatonic;
         extra.appliedCapo = params.capo;
         extra.appliedDiatonic = params.diatonic;
         if (!changed || !params.retrigger)
            return;
         for (int key = 0; key < 128; ++key)
         {
            if (held.out[key] < 0)
               continue;
            writer.push(held.out[key], 0.0f, false, held.channel[key]);
            const int pitch = shifted(key, params, block.scale);
            held.out[key] = static_cast<int8_t>(pitch);
            writer.push(pitch, held.velocity[key], true, held.channel[key], 0, held.inputBend[key]);
         }
      }

      BESPOKE_REGISTER_MODULE(CapoModuleAdapter);

   } // namespace wasm
} // namespace bespoke
