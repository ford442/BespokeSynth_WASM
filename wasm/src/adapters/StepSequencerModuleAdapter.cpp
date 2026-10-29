/**
 * BespokeSynth WASM - Step sequencer adapter implementation
 */

#include "BespokeWasm/adapters/StepSequencerModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr double kStepsPerBeat = 4.0; // 16th notes

         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "pattern", .minValue = 0.0f, .maxValue = 65535.0f, .defaultValue = static_cast<float>(0x1111), .kind = ParamKind::Mask16, .offset = offsetof(StepSequencerParams, patternMask) },
            { .id = 1, .name = "pitch", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f, .kind = ParamKind::IntRound, .offset = offsetof(StepSequencerParams, pitch) },
            { .id = 2, .name = "gate", .minValue = 0.05f, .maxValue = 1.0f, .defaultValue = 0.75f, .offset = offsetof(StepSequencerParams, gate), .flags = kParamClamp },
            { .id = 3, .name = "steps", .minValue = 1.0f, .maxValue = 16.0f, .defaultValue = 16.0f, .kind = ParamKind::IntRound, .offset = offsetof(StepSequencerParams, steps), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kOutputs[] = { { PortType::Note, "Notes" } };
      }

      ParamTable StepSequencerModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> StepSequencerModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> StepSequencerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<StepSequencerModule>(id);
      }

      void StepSequencerModuleAdapter::emitNotesForBeatRange(void* runtimeState,
                                                             const void* paramsPtr,
                                                             double beatStart,
                                                             double beatEnd,
                                                             WasmNoteEvent* outNotes,
                                                             int maxNotes,
                                                             int& outCount,
                                                             const WasmPulseEvent* pulses,
                                                             int pulseCount) const
      {
         (void)pulses;
         (void)pulseCount;
         outCount = 0;
         if (!outNotes || maxNotes <= 0 || !runtimeState || !paramsPtr)
            return;

         auto& state = *static_cast<StepSequencerRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const StepSequencerParams*>(paramsPtr);
         const int stepCount = std::max(1, std::min(16, params.steps));
         const int pitch = std::max(0, std::min(127, params.pitch));
         const float gate = clampFloat(params.gate, 0.05f, 1.0f);
         const double stepBeats = 1.0 / kStepsPerBeat;

         auto pushNote = [&](const WasmNoteEvent& event)
         {
            if (outCount < maxNotes)
               outNotes[outCount++] = event;
         };

         auto maybeNoteOff = [&](double limitBeat)
         {
            if (state.noteIsOn && state.noteOffBeat >= 0.0 &&
                state.noteOffBeat > beatStart && state.noteOffBeat <= limitBeat)
            {
               pushNote({ state.activePitch, 0.0f, false });
               state.noteIsOn = false;
               state.noteOffBeat = -1.0;
            }
         };

         maybeNoteOff(beatEnd);

         const double startStep = beatStart * kStepsPerBeat;
         const double endStep = beatEnd * kStepsPerBeat;
         const int firstStep = static_cast<int>(std::ceil(startStep - 1e-12));
         const int lastStep = static_cast<int>(std::floor(endStep - 1e-12));

         for (int absoluteStep = firstStep; absoluteStep <= lastStep; ++absoluteStep)
         {
            if (absoluteStep < 0 || absoluteStep == state.lastEmittedStep)
               continue;
            state.lastEmittedStep = absoluteStep;

            const double stepBeat = static_cast<double>(absoluteStep) / kStepsPerBeat;
            maybeNoteOff(stepBeat);

            const int patternStep = ((absoluteStep % stepCount) + stepCount) % stepCount;
            if ((params.patternMask & (1 << patternStep)) == 0)
               continue;

            if (state.noteIsOn)
            {
               pushNote({ state.activePitch, 0.0f, false });
               state.noteIsOn = false;
            }

            pushNote({ pitch, 1.0f, true });
            state.noteIsOn = true;
            state.activePitch = pitch;
            state.noteOffBeat = stepBeat + gate * stepBeats;
         }

         maybeNoteOff(beatEnd);
      }

      BESPOKE_REGISTER_MODULE(StepSequencerModuleAdapter);

   } // namespace wasm
} // namespace bespoke
