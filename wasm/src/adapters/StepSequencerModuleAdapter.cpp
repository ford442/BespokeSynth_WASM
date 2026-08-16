/**
 * BespokeSynth WASM - Step sequencer adapter implementation
 */

#include "BespokeWasm/adapters/StepSequencerModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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

      std::unique_ptr<Module> StepSequencerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<StepSequencerModule>(id);
      }

      void StepSequencerModuleAdapter::fillAudioGraphNode(const std::map<std::string, float>& controls,
                                                          AudioGraphNode& node) const
      {
         auto get = [&](const char* name, float fallback) -> float
         {
            auto it = controls.find(name);
            return it != controls.end() ? it->second : fallback;
         };
         node.patternMask = static_cast<int>(get("pattern", static_cast<float>(0x1111))) & 0xFFFF;
         node.seqPitch = static_cast<int>(std::lround(get("pitch", 60.0f)));
         node.gateLength = clampFloat(get("gate", 0.75f), 0.05f, 1.0f);
         node.steps = std::max(1, std::min(16, static_cast<int>(std::lround(get("steps", 16.0f)))));
      }

      void StepSequencerModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         std::memset(runtimeState, 0, sizeof(StepSequencerRuntimeState));
         auto* state = static_cast<StepSequencerRuntimeState*>(runtimeState);
         state->lastEmittedStep = -1;
         state->noteOffBeat = -1.0;
      }

      void StepSequencerModuleAdapter::emitNotesForBeatRange(void* runtimeState,
                                                             const AudioGraphNode& node,
                                                             double beatStart,
                                                             double beatEnd,
                                                             WasmNoteEvent* outNotes,
                                                             int maxNotes,
                                                             int& outCount) const
      {
         outCount = 0;
         if (!outNotes || maxNotes <= 0)
            return;

         auto& state = *static_cast<StepSequencerRuntimeState*>(runtimeState);
         const int stepCount = std::max(1, std::min(16, node.steps));
         const int pitch = std::max(0, std::min(127, node.seqPitch));
         const float gate = clampFloat(node.gateLength, 0.05f, 1.0f);
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
            if ((node.patternMask & (1 << patternStep)) == 0)
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

   } // namespace wasm
} // namespace bespoke
