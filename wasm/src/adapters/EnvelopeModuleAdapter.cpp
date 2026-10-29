/**
 * BespokeSynth WASM - Envelope modulator adapter implementation
 */

#include "BespokeWasm/adapters/EnvelopeModuleAdapter.h"
#include "BespokeWasm/ModulationTriggers.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "attack", .minValue = 1.0f, .maxValue = 2000.0f, .defaultValue = 10.0f, .offset = offsetof(EnvelopeParams, attack), .flags = kParamClamp },
            { .id = 1, .name = "decay", .minValue = 1.0f, .maxValue = 2000.0f, .defaultValue = 200.0f, .offset = offsetof(EnvelopeParams, decay), .flags = kParamClamp },
            { .id = 2, .name = "sustain", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(EnvelopeParams, sustain), .flags = kParamClamp },
            { .id = 3, .name = "release", .minValue = 1.0f, .maxValue = 4000.0f, .defaultValue = 300.0f, .offset = offsetof(EnvelopeParams, release), .flags = kParamClamp },
            { .id = 4, .name = "low", .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .offset = offsetof(EnvelopeParams, low), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 5, .name = "high", .minValue = -1.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .offset = offsetof(EnvelopeParams, high), .smoothingMs = kParamSmoothingMs, .flags = kParamClamp },
            { .id = 6, .name = "useVelocity", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::Bool, .offset = offsetof(EnvelopeParams, useVelocity) },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Trigger" }, { PortType::Pulse, "Pulse" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Modulation, "Mod" } };

         const std::vector<SliderSpec>& envelopeSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "attack", "Attack", SliderFormat::Millis, {}, true },
                                                                           { "decay", "Decay", SliderFormat::Millis, {}, true },
                                                                           { "sustain", "Sustain", SliderFormat::Percent },
                                                                           { "release", "Release", SliderFormat::Millis, {}, true },
                                                                           { "low", "Low", SliderFormat::Decimal },
                                                                           { "high", "High", SliderFormat::Decimal },
                                                                           { "useVelocity", "Velocity", SliderFormat::Choice, { "off", "on" } },
                                                                           });
            return specs;
         }

         void render(EnvelopeAdapterRuntimeState& state, const EnvelopeParams& params, float* out, int from, int to)
         {
            const float span = params.high - params.low;
            for (int i = from; i < to; ++i)
            {
               const float level = state.envelope.next();
               if (state.oneShot && state.envelope.stage() == dsp::Envelope::Stage::Sustain)
               {
                  state.oneShot = false;
                  if (state.heldNotes == 0)
                     state.envelope.stop();
               }
               out[i] = params.low + span * level;
            }
         }
      }

      ParamTable EnvelopeModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> EnvelopeModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> EnvelopeModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> EnvelopeModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), envelopeSpecs(), 160.0f);
      }

      void EnvelopeModuleAdapter::processAudio(void* runtimeState,
                                               const void* paramsPtr,
                                               float* const* channels,
                                               const WasmAudioProcessContext& context) const
      {
         float* out = channels ? channels[0] : nullptr;
         if (!runtimeState || !paramsPtr || !out)
            return;
         auto& state = *static_cast<EnvelopeAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const EnvelopeParams*>(paramsPtr);
         state.envelope.prepare({ params.attack, params.decay, params.sustain, params.release }, context.sampleRate);

         // Notes have no in-block time and apply at frame 0; pulses split the block on their frame.
         for (int n = 0; n < context.noteCount; ++n)
         {
            const WasmNoteEvent& note = context.notes[n];
            if (isPitchBendEvent(note) || note.pitch < 0 || note.pitch > 127)
               continue;
            bool& held = state.held[note.pitch];
            if (note.isNoteOn && note.velocity > 0.0f)
            {
               if (!held)
                  ++state.heldNotes;
               held = true;
               state.oneShot = false;
               state.envelope.start(params.useVelocity ? std::min(1.0f, note.velocity) : 1.0f);
            }
            else if (held)
            {
               held = false;
               if (--state.heldNotes <= 0)
               {
                  state.heldNotes = 0;
                  state.envelope.stop();
               }
            }
         }

         int frame = 0;
         for (int p = 0; p < context.pulseCount; ++p)
         {
            const int at = std::max(frame, beatToBlockFrame(context, context.pulses[p].timeBeats));
            render(state, params, out, frame, at);
            frame = at;
            state.oneShot = true;
            state.envelope.start(params.useVelocity ? std::max(0.0f, std::min(1.0f, context.pulses[p].strength)) : 1.0f);
         }
         render(state, params, out, frame, context.numSamples);
      }

      BESPOKE_REGISTER_MODULE(EnvelopeModuleAdapter);

   } // namespace wasm
} // namespace bespoke
