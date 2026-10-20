/**
 * BespokeSynth WASM - Pitch bender adapter implementation
 */

#include "BespokeWasm/adapters/PitchBenderModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <cmath>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "bend", .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .offset = offsetof(PitchBenderParams, bend), .flags = kParamClamp },
            { .id = 1, .name = "range", .minValue = 0.0f, .maxValue = 24.0f, .defaultValue = 2.0f, .offset = offsetof(PitchBenderParams, range), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Note, "Out" } };

         // Below this a bend update is inaudible; skipping it keeps an idle bender from flooding the note scratch.
         constexpr float kBendEpsilon = 1e-4f;

         const std::vector<SliderSpec>& benderSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "bend", "Bend", SliderFormat::Decimal },
                                                                           { "range", "Range", SliderFormat::Decimal },
                                                                           });
            return specs;
         }
      }

      ParamTable PitchBenderModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> PitchBenderModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> PitchBenderModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> PitchBenderModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), benderSpecs(), 140.0f);
      }

      bool PitchBenderModuleAdapter::transformNoteOn(PitchBenderExtraState& extra,
                                                     const PitchBenderParams& params,
                                                     const WasmNoteProcessBlock& block,
                                                     const WasmNoteEvent& in,
                                                     int& pitch,
                                                     float& velocity) const
      {
         (void)extra;
         (void)params;
         (void)block;
         (void)in;
         (void)pitch;
         (void)velocity;
         return true;
      }

      float PitchBenderModuleAdapter::transformBend(const PitchBenderExtraState& extra,
                                                    const PitchBenderParams& params,
                                                    float inputBend) const
      {
         (void)extra;
         return inputBend + semitones(params);
      }

      void PitchBenderModuleAdapter::beginBlock(PitchBenderExtraState& extra,
                                                const PitchBenderParams& params,
                                                const WasmNoteProcessBlock& block,
                                                NoteTransformHeld& held,
                                                NoteWriter& writer) const
      {
         (void)block;
         const float target = semitones(params);
         if (std::fabs(target - extra.appliedSemitones) < kBendEpsilon)
            return;
         extra.appliedSemitones = target;
         held.forEachSounding([&](int key, int sounding)
                              {
                                 writer.pushBend(sounding, held.inputBend[key] + target, held.channel[key]);
                              });
      }

      BESPOKE_REGISTER_MODULE(PitchBenderModuleAdapter);

   } // namespace wasm
} // namespace bespoke
