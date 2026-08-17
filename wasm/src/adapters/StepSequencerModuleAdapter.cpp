/**
 * BespokeSynth WASM - Step sequencer adapter implementation
 */

#include "BespokeWasm/adapters/StepSequencerModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <cmath>
#include <new>

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
      }

      std::vector<WasmControlDescriptor> StepSequencerModuleAdapter::controlDescriptors() const
      {
         return {
            { "pattern", static_cast<float>(0x1111) },
            { "pitch", 60.0f },
            { "gate", 0.75f },
            { "steps", 16.0f },
         };
      }

      std::vector<PortDescriptor> StepSequencerModuleAdapter::inputPorts() const
      {
         return {};
      }

      std::vector<PortDescriptor> StepSequencerModuleAdapter::outputPorts() const
      {
         return { { PortType::Note, "Notes" } };
      }

      std::unique_ptr<Module> StepSequencerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<StepSequencerModule>(id);
      }

      void StepSequencerModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) StepSequencerParams();
         params->patternMask = static_cast<int>(wasmControlValue(controls, "pattern", static_cast<float>(0x1111))) & 0xFFFF;
         params->pitch = static_cast<int>(std::lround(wasmControlValue(controls, "pitch", 60.0f)));
         params->gate = clampFloat(wasmControlValue(controls, "gate", 0.75f), 0.05f, 1.0f);
         params->steps = std::max(1, std::min(16, static_cast<int>(std::lround(wasmControlValue(controls, "steps", 16.0f)))));
      }

      void StepSequencerModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) StepSequencerRuntimeState();
      }

      void StepSequencerModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<StepSequencerRuntimeState*>(runtimeState)->~StepSequencerRuntimeState();
      }

      void StepSequencerModuleAdapter::emitNotesForBeatRange(void* runtimeState,
                                                             const void* paramsPtr,
                                                             double beatStart,
                                                             double beatEnd,
                                                             WasmNoteEvent* outNotes,
                                                             int maxNotes,
                                                             int& outCount) const
      {
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
