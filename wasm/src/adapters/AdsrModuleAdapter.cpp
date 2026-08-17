/**
 * BespokeSynth WASM - ADSR envelope adapter implementation
 */

#include "BespokeWasm/adapters/AdsrModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <algorithm>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }
      }

      std::vector<WasmControlDescriptor> AdsrModuleAdapter::controlDescriptors() const
      {
         return {
            { "attack", 10.0f },
            { "decay", 120.0f },
            { "sustain", 0.7f },
            { "release", 200.0f },
         };
      }

      std::vector<PortDescriptor> AdsrModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" }, { PortType::Note, "Gate" } };
      }

      std::vector<PortDescriptor> AdsrModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> AdsrModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<AdsrModule>(id);
      }

      void AdsrModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) AdsrParams();
         params->attack = wasmControlValue(controls, "attack", 10.0f);
         params->decay = wasmControlValue(controls, "decay", 120.0f);
         params->sustain = wasmControlValue(controls, "sustain", 0.7f);
         params->release = wasmControlValue(controls, "release", 200.0f);
      }

      void AdsrModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) AdsrAdapterRuntimeState();
      }

      void AdsrModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<AdsrAdapterRuntimeState*>(runtimeState)->~AdsrAdapterRuntimeState();
      }

      void AdsrModuleAdapter::processAudio(void* runtimeState,
                                           const void* paramsPtr,
                                           float* buffer,
                                           const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<AdsrAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const AdsrParams*>(paramsPtr);
         const float attackSec = clampFloat(params.attack, 1.0f, 5000.0f) * 0.001f;
         const float decaySec = clampFloat(params.decay, 1.0f, 5000.0f) * 0.001f;
         const float sustain = clampFloat(params.sustain, 0.0f, 1.0f);
         const float releaseSec = clampFloat(params.release, 1.0f, 5000.0f) * 0.001f;
         const float sampleDt = 1.0f / std::max(1.0f, context.sampleRate);

         for (int n = 0; n < context.noteCount; ++n)
         {
            const WasmNoteEvent& note = context.notes[n];
            if (note.isNoteOn)
            {
               state.stage = AdsrAdapterRuntimeState::Stage::Attack;
               state.stageTimeSeconds = 0.0f;
               state.hasReceivedNote = true;
            }
            else
            {
               state.stage = AdsrAdapterRuntimeState::Stage::Release;
               state.stageTimeSeconds = 0.0f;
               state.hasReceivedNote = true;
            }
         }

         for (int i = 0; i < context.numSamples; ++i)
         {
            if (!state.hasReceivedNote)
            {
               // Transparent until the first note so free-running patches stay audible.
               continue;
            }

            state.stageTimeSeconds += sampleDt;
            switch (state.stage)
            {
               case AdsrAdapterRuntimeState::Stage::Attack:
               {
                  const float t = attackSec <= 0.0f ? 1.0f : state.stageTimeSeconds / attackSec;
                  state.level = clampFloat(t, 0.0f, 1.0f);
                  if (t >= 1.0f)
                  {
                     state.stage = AdsrAdapterRuntimeState::Stage::Decay;
                     state.stageTimeSeconds = 0.0f;
                     state.level = 1.0f;
                  }
                  break;
               }
               case AdsrAdapterRuntimeState::Stage::Decay:
               {
                  const float t = decaySec <= 0.0f ? 1.0f : state.stageTimeSeconds / decaySec;
                  state.level = 1.0f + (sustain - 1.0f) * clampFloat(t, 0.0f, 1.0f);
                  if (t >= 1.0f)
                  {
                     state.stage = AdsrAdapterRuntimeState::Stage::Sustain;
                     state.level = sustain;
                  }
                  break;
               }
               case AdsrAdapterRuntimeState::Stage::Sustain:
                  state.level = sustain;
                  break;
               case AdsrAdapterRuntimeState::Stage::Release:
               {
                  const float start = std::max(state.level, 0.0001f);
                  const float t = releaseSec <= 0.0f ? 1.0f : state.stageTimeSeconds / releaseSec;
                  state.level = start * (1.0f - clampFloat(t, 0.0f, 1.0f));
                  if (t >= 1.0f)
                  {
                     state.stage = AdsrAdapterRuntimeState::Stage::Idle;
                     state.level = 0.0f;
                  }
                  break;
               }
               case AdsrAdapterRuntimeState::Stage::Idle:
               default:
                  state.level = 0.0f;
                  break;
            }

            buffer[i] *= state.level;
         }
      }

      BESPOKE_REGISTER_MODULE(AdsrModuleAdapter);

   } // namespace wasm
} // namespace bespoke
