/**
 * BespokeSynth WASM - Looper adapter
 */

#include "BespokeWasm/adapters/LooperModuleAdapter.h"
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
         int quantizedLengthFrames(int bars, float bpm, float sampleRate)
         {
            const float safeBpm = std::max(20.0f, bpm);
            const double seconds = static_cast<double>(std::max(1, bars)) * 4.0 * 60.0 / safeBpm;
            const int frames = static_cast<int>(std::llround(seconds * sampleRate));
            return std::max(1, std::min(frames, LooperArena::kMaxFrames));
         }

         double nextBarBeat(double beat)
         {
            const double bar = 4.0;
            return std::ceil((beat + 1.0e-9) / bar) * bar;
         }
      }

      std::vector<WasmControlDescriptor> LooperModuleAdapter::controlDescriptors() const
      {
         return {
            { "mix", 1.0f },
            { "bars", 1.0f },
            { "command", 0.0f },
         };
      }

      std::vector<PortDescriptor> LooperModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" } };
      }

      std::vector<PortDescriptor> LooperModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> LooperModuleAdapter::createUiModule(int id) const
      {
         LooperArenaPool::instance().ensure(id);
         return std::make_unique<LooperModule>(id);
      }

      void LooperModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         fillParams(-1, controls, {}, dst);
      }

      void LooperModuleAdapter::fillParams(int moduleId,
                                           const WasmControlMap& controls,
                                           const WasmStringMap& extras,
                                           void* dst) const
      {
         (void)extras;
         auto* params = new (dst) LooperParams();
         params->arena = LooperArenaPool::instance().find(moduleId);
         if (!params->arena)
            params->arena = LooperArenaPool::instance().ensure(moduleId);
         params->mix = std::max(0.0f, std::min(1.0f, wasmControlValue(controls, "mix", 1.0f)));
         params->bars = std::max(1, std::min(8, static_cast<int>(std::lround(wasmControlValue(controls, "bars", 1.0f)))));
         params->command = static_cast<int>(std::lround(wasmControlValue(controls, "command", 0.0f)));
      }

      void LooperModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) LooperAdapterRuntimeState();
      }

      void LooperModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<LooperAdapterRuntimeState*>(runtimeState)->~LooperAdapterRuntimeState();
      }

      void LooperModuleAdapter::processAudio(void* runtimeState,
                                             const void* paramsPtr,
                                             float* buffer,
                                             const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !buffer)
            return;

         auto& state = *static_cast<LooperAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const LooperParams*>(paramsPtr);
         LooperArena* arena = params.arena;
         if (!arena)
            return;

         if (params.command != state.lastCommand)
         {
            if (params.command == 1 || params.command == 2)
            {
               arena->pendingMode = params.command;
               arena->state = 1;
               arena->queuedBarBeat = nextBarBeat(context.beatStart);
            }
            else if (params.command == 3)
            {
               arena->state = arena->hasLoop ? 3 : 0;
               arena->playPos = 0;
            }
            else if (params.command == 4)
            {
               arena->state = 0;
            }
            state.lastCommand = params.command;
         }

         const int targetLength = quantizedLengthFrames(params.bars, context.bpm, context.sampleRate);

         for (int i = 0; i < context.numSamples; ++i)
         {
            const double beat = context.beatStart +
                                (context.beatEnd - context.beatStart) * (static_cast<double>(i) / std::max(1, context.numSamples));
            const float live = context.hasAudioInput
                               ? buffer[i]
                               : (context.inputAudio ? context.inputAudio[i] : 0.0f);

            if (arena->state == 1 && beat >= arena->queuedBarBeat)
            {
               if (arena->pendingMode == 1)
               {
                  std::fill(arena->samples.begin(), arena->samples.end(), 0.0f);
                  arena->writePos = 0;
                  arena->loopLength = targetLength;
                  arena->hasLoop = false;
                  arena->state = 2;
               }
               else
               {
                  arena->writePos = 0;
                  arena->state = 4;
               }
            }

            if (arena->state == 2)
            {
               if (arena->writePos < arena->loopLength)
                  arena->samples[static_cast<size_t>(arena->writePos)] = live;
               ++arena->writePos;
               if (arena->writePos >= arena->loopLength)
               {
                  arena->hasLoop = true;
                  arena->playPos = 0;
                  arena->state = 3;
               }
            }
            else if (arena->state == 4 && arena->loopLength > 0)
            {
               const int pos = arena->writePos % arena->loopLength;
               arena->samples[static_cast<size_t>(pos)] =
               std::max(-1.0f, std::min(1.0f, arena->samples[static_cast<size_t>(pos)] + live));
               arena->writePos = (pos + 1) % arena->loopLength;
            }

            float loopSample = 0.0f;
            if ((arena->state == 3 || arena->state == 4) && arena->loopLength > 0)
            {
               loopSample = arena->samples[static_cast<size_t>(arena->playPos)];
               arena->playPos = (arena->playPos + 1) % arena->loopLength;
            }
            buffer[i] = live * (1.0f - params.mix) + loopSample * params.mix;
         }
      }

      BESPOKE_REGISTER_MODULE(LooperModuleAdapter);

   } // namespace wasm
} // namespace bespoke
