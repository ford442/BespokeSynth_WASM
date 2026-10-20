/**
 * BespokeSynth WASM - Ramper modulator adapter implementation
 */

#include "BespokeWasm/adapters/RamperModuleAdapter.h"
#include "BespokeWasm/ModulationTriggers.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr double kLengthBeats[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
         constexpr int kLengthCount = static_cast<int>(sizeof(kLengthBeats) / sizeof(kLengthBeats[0]));

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "start", .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .offset = offsetof(RamperParams, start), .flags = kParamClamp },
            { .id = 1, .name = "target", .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(RamperParams, target), .flags = kParamClamp },
            { .id = 2, .name = "length", .minValue = 0.0f, .maxValue = static_cast<float>(kLengthCount - 1), .defaultValue = 4.0f, .kind = ParamKind::IntRound, .offset = offsetof(RamperParams, length), .flags = kParamClamp },
            { .id = 3, .name = "mode", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::IntRound, .offset = offsetof(RamperParams, mode), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Trigger" }, { PortType::Pulse, "Pulse" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Modulation, "Mod" } };

         const std::vector<SliderSpec>& ramperSpecs()
         {
            static const std::vector<SliderSpec> specs =
            slidersFromParams(kParams,
                              {
                              { "start", "Start", SliderFormat::Decimal },
                              { "target", "Target", SliderFormat::Decimal },
                              { "length", "Length", SliderFormat::Choice, { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" } },
                              { "mode", "Mode", SliderFormat::Choice, { "one-shot", "loop" } },
                              });
            return specs;
         }

         void renderOneShot(RamperAdapterRuntimeState& state,
                            const RamperParams& params,
                            double step,
                            float* out,
                            int from,
                            int to)
         {
            for (int i = from; i < to; ++i)
            {
               const double t = state.progress < 0.0 ? 0.0 : std::min(1.0, state.progress);
               out[i] = params.start + (params.target - params.start) * static_cast<float>(t);
               if (state.progress >= 0.0 && state.progress < 1.0)
                  state.progress += step;
            }
         }
      }

      double RamperModuleAdapter::lengthBeats(int choice)
      {
         return kLengthBeats[std::max(0, std::min(kLengthCount - 1, choice))];
      }

      ParamTable RamperModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> RamperModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> RamperModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> RamperModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), ramperSpecs(), 150.0f);
      }

      void RamperModuleAdapter::processAudio(void* runtimeState,
                                             const void* paramsPtr,
                                             float* const* channels,
                                             const WasmAudioProcessContext& context) const
      {
         float* out = channels ? channels[0] : nullptr;
         if (!runtimeState || !paramsPtr || !out || context.numSamples <= 0)
            return;
         auto& state = *static_cast<RamperAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const RamperParams*>(paramsPtr);
         const double length = lengthBeats(params.length);

         if (params.mode == 1)
         {
            // Locked to the transport so it stays in phase with the sequencers.
            const double beatsPerFrame = (context.beatEnd - context.beatStart) / context.numSamples;
            for (int i = 0; i < context.numSamples; ++i)
            {
               const double beat = context.beatStart + beatsPerFrame * i;
               const double phase = beat / length - std::floor(beat / length);
               out[i] = params.start + (params.target - params.start) * static_cast<float>(phase);
            }
            return;
         }

         // Tempo-timed rather than transport-timed, so a trigger ramps while the transport is stopped too.
         const double step = static_cast<double>(context.bpm) / 60.0 / std::max(1.0f, context.sampleRate) / length;
         bool noteTrigger = false;
         for (int n = 0; n < context.noteCount; ++n)
         {
            const WasmNoteEvent& note = context.notes[n];
            if (!isPitchBendEvent(note) && note.isNoteOn && note.velocity > 0.0f)
               noteTrigger = true;
         }
         if (noteTrigger)
            state.progress = 0.0;

         int frame = 0;
         for (int p = 0; p < context.pulseCount; ++p)
         {
            const int at = std::max(frame, beatToBlockFrame(context, context.pulses[p].timeBeats));
            renderOneShot(state, params, step, out, frame, at);
            frame = at;
            state.progress = 0.0;
         }
         renderOneShot(state, params, step, out, frame, context.numSamples);
      }

      BESPOKE_REGISTER_MODULE(RamperModuleAdapter);

   } // namespace wasm
} // namespace bespoke
