/**
 * BespokeSynth WASM - Transport singleton adapter implementation
 */

#include "BespokeWasm/adapters/TransportModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <cmath>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr double kStepsPerBeat = 4.0;
      }

      std::vector<WasmControlDescriptor> TransportModuleAdapter::controlDescriptors() const
      {
         return { { "bpm", 120.0f }, { "swing", 0.0f } };
      }

      std::vector<PortDescriptor> TransportModuleAdapter::outputPorts() const
      {
         return { { PortType::Pulse, "Clock" } };
      }

      std::unique_ptr<Module> TransportModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<TransportModule>(id);
      }

      void TransportModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) TransportPulseRuntimeState();
      }

      void TransportModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<TransportPulseRuntimeState*>(runtimeState)->~TransportPulseRuntimeState();
      }

      void TransportModuleAdapter::emitPulsesForBeatRange(void* runtimeState,
                                                          const void* params,
                                                          double beatStart,
                                                          double beatEnd,
                                                          WasmPulseEvent* outPulses,
                                                          int maxPulses,
                                                          int& outCount) const
      {
         (void)params;
         outCount = 0;
         if (!outPulses || maxPulses <= 0 || !runtimeState)
            return;

         auto& state = *static_cast<TransportPulseRuntimeState*>(runtimeState);
         const double startStep = beatStart * kStepsPerBeat;
         const double endStep = beatEnd * kStepsPerBeat;
         const int firstStep = static_cast<int>(std::ceil(startStep - 1e-12));
         const int lastStep = static_cast<int>(std::floor(endStep - 1e-12));

         for (int absoluteStep = firstStep; absoluteStep <= lastStep; ++absoluteStep)
         {
            if (absoluteStep < 0 || absoluteStep == state.lastEmittedStep)
               continue;
            state.lastEmittedStep = absoluteStep;
            if (outCount >= maxPulses)
               break;
            outPulses[outCount++] = { static_cast<double>(absoluteStep) / kStepsPerBeat, 1.0f };
         }
      }

      BESPOKE_REGISTER_MODULE(TransportModuleAdapter);

   } // namespace wasm
} // namespace bespoke
