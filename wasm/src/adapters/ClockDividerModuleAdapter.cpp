/**
 * BespokeSynth WASM - Clock divider (Pulse processor) adapter implementation
 */

#include "BespokeWasm/adapters/ClockDividerModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <cmath>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         const std::vector<SliderSpec>& clockDividerSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "divide", "Divide", 1.0f, 32.0f, 4.0f, SliderFormat::Integer },
               { "offset", "Offset", 0.0f, 31.0f, 0.0f, SliderFormat::Integer },
            };
            return specs;
         }
      }

      std::vector<WasmControlDescriptor> ClockDividerModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(clockDividerSpecs());
      }

      std::vector<PortDescriptor> ClockDividerModuleAdapter::inputPorts() const
      {
         return { { PortType::Pulse, "Clock" } };
      }

      std::vector<PortDescriptor> ClockDividerModuleAdapter::outputPorts() const
      {
         return { { PortType::Pulse, "Out" } };
      }

      std::unique_ptr<Module> ClockDividerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), clockDividerSpecs(), 140.0f);
      }

      void ClockDividerModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         const auto& specs = clockDividerSpecs();
         auto* params = new (dst) ClockDividerParams();
         params->divide = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "divide")));
         params->offset = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "offset")));
      }

      void ClockDividerModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) ClockDividerRuntimeState();
      }

      void ClockDividerModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<ClockDividerRuntimeState*>(runtimeState)->~ClockDividerRuntimeState();
      }

      void ClockDividerModuleAdapter::emitPulsesForBeatRange(void* runtimeState,
                                                             const void* paramsPtr,
                                                             double beatStart,
                                                             double beatEnd,
                                                             WasmPulseEvent* outPulses,
                                                             int maxPulses,
                                                             int& outCount,
                                                             const WasmPulseEvent* inputPulses,
                                                             int inputPulseCount) const
      {
         (void)beatStart;
         (void)beatEnd;
         outCount = 0;
         if (!outPulses || maxPulses <= 0 || !runtimeState || !paramsPtr || !inputPulses)
            return;

         auto& state = *static_cast<ClockDividerRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const ClockDividerParams*>(paramsPtr);
         const int divide = params.divide < 1 ? 1 : params.divide;
         const int offset = ((params.offset % divide) + divide) % divide;

         // Every Nth incoming pulse passes through with its time and strength intact.
         for (int i = 0; i < inputPulseCount; ++i)
         {
            const long long index = state.pulseCount++;
            if (index % divide != offset)
               continue;
            if (outCount < maxPulses)
               outPulses[outCount++] = inputPulses[i];
         }
      }

      BESPOKE_REGISTER_MODULE(ClockDividerModuleAdapter);

   } // namespace wasm
} // namespace bespoke
