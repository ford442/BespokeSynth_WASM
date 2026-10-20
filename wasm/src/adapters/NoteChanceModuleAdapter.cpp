/**
 * BespokeSynth WASM - Note chance adapter implementation
 */

#include "BespokeWasm/adapters/NoteChanceModuleAdapter.h"
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
            { .id = 0, .name = "chance", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(NoteChanceParams, chance), .flags = kParamClamp },
            { .id = 1, .name = "deterministic", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Bool, .offset = offsetof(NoteChanceParams, deterministic) },
            { .id = 2, .name = "length", .minValue = 1.0f, .maxValue = 16.0f, .defaultValue = 4.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteChanceParams, length), .flags = kParamClamp },
            { .id = 3, .name = "seed", .minValue = 0.0f, .maxValue = 999.0f, .defaultValue = 0.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteChanceParams, seed), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Note, "Out" } };

         const std::vector<SliderSpec>& chanceSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "chance", "Chance", SliderFormat::Percent },
                                                                           { "deterministic", "Deterministic", SliderFormat::Choice, { "off", "on" } },
                                                                           { "length", "Length", SliderFormat::Integer },
                                                                           { "seed", "Seed", SliderFormat::Integer },
                                                                           });
            return specs;
         }

         uint32_t mix(uint32_t x)
         {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
         }

         float unitFloat(uint32_t x)
         {
            return static_cast<float>(x >> 8) / 16777216.0f;
         }
      }

      float NoteChanceModuleAdapter::deterministicRoll(const NoteChanceParams& params, double beat, int pitch)
      {
         const int length = params.length < 1 ? 1 : params.length;
         // A tiny epsilon keeps a note scheduled exactly on a step from landing on the one before.
         const auto sixteenth = static_cast<int64_t>(std::floor(beat * 4.0 + 1e-6));
         const auto step = static_cast<uint32_t>(((sixteenth % length) + length) % length);
         const uint32_t key = mix(static_cast<uint32_t>(params.seed) * 0x9e3779b9u ^ mix(step + 1u)) ^
                              mix(static_cast<uint32_t>(pitch) + 0x51ed27u);
         return unitFloat(mix(key));
      }

      ParamTable NoteChanceModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> NoteChanceModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> NoteChanceModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> NoteChanceModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), chanceSpecs(), 150.0f);
      }

      bool NoteChanceModuleAdapter::transformNoteOn(NoteChanceExtraState& extra,
                                                    const NoteChanceParams& params,
                                                    const WasmNoteProcessBlock& block,
                                                    const WasmNoteEvent& in,
                                                    int& pitch,
                                                    float& velocity) const
      {
         (void)pitch;
         (void)velocity;
         if (params.chance >= 1.0f)
            return true;
         if (params.chance <= 0.0f)
            return false;
         if (params.deterministic)
            return deterministicRoll(params, block.beatStart, in.pitch) < params.chance;
         if (extra.seededWith != params.seed)
         {
            extra.seededWith = params.seed;
            extra.rng = mix(static_cast<uint32_t>(params.seed) + 0x2545f491u) | 1u;
         }
         extra.rng ^= extra.rng << 13;
         extra.rng ^= extra.rng >> 17;
         extra.rng ^= extra.rng << 5;
         return unitFloat(extra.rng) < params.chance;
      }

      BESPOKE_REGISTER_MODULE(NoteChanceModuleAdapter);

   } // namespace wasm
} // namespace bespoke
