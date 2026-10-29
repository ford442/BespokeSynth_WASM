/**
 * BespokeSynth WASM - Pulse to Note converter adapter implementation
 */

#include "BespokeWasm/adapters/PulseToNoteModuleAdapter.h"
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
         constexpr double kStepsPerBeat = 4.0;

         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "pitch", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = 60.0f, .kind = ParamKind::IntRound, .offset = offsetof(PulseToNoteParams, pitch) },
            { .id = 1, .name = "gate", .minValue = 0.05f, .maxValue = 1.0f, .defaultValue = 0.75f, .offset = offsetof(PulseToNoteParams, gate), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Pulse, "Pulse" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Note, "Notes" } };
      }

      ParamTable PulseToNoteModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> PulseToNoteModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> PulseToNoteModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> PulseToNoteModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<PulseToNoteModule>(id);
      }

      void PulseToNoteModuleAdapter::emitNotesForBeatRange(void* runtimeState,
                                                           const void* paramsPtr,
                                                           double beatStart,
                                                           double beatEnd,
                                                           WasmNoteEvent* outNotes,
                                                           int maxNotes,
                                                           int& outCount,
                                                           const WasmPulseEvent* pulses,
                                                           int pulseCount) const
      {
         outCount = 0;
         if (!outNotes || maxNotes <= 0 || !runtimeState || !paramsPtr)
            return;

         auto& state = *static_cast<PulseToNoteRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const PulseToNoteParams*>(paramsPtr);
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

         for (int i = 0; i < pulseCount; ++i)
         {
            const double pulseBeat = pulses[i].timeBeats;
            if (pulseBeat < beatStart || pulseBeat >= beatEnd)
               continue;
            maybeNoteOff(pulseBeat);
            if (state.noteIsOn)
            {
               pushNote({ state.activePitch, 0.0f, false });
               state.noteIsOn = false;
            }
            pushNote({ pitch, pulses[i].strength, true });
            state.noteIsOn = true;
            state.activePitch = pitch;
            state.noteOffBeat = pulseBeat + gate * stepBeats;
         }

         maybeNoteOff(beatEnd);
      }

      BESPOKE_REGISTER_MODULE(PulseToNoteModuleAdapter);

   } // namespace wasm
} // namespace bespoke
