/**
 * BespokeSynth WASM - Clock divider (Pulse processor) adapter implementation
 */

#include "BespokeWasm/adapters/ClockDividerModuleAdapter.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "divide", .minValue = 1.0f, .maxValue = 32.0f, .defaultValue = 4.0f, .kind = ParamKind::IntRound, .offset = offsetof(ClockDividerParams, divide), .flags = kParamClamp },
            { .id = 1, .name = "offset", .minValue = 0.0f, .maxValue = 31.0f, .defaultValue = 0.0f, .kind = ParamKind::IntRound, .offset = offsetof(ClockDividerParams, offset), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Pulse, "Clock" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Pulse, "Out" } };

         const std::vector<SliderSpec>& clockDividerSpecs()
         {
            static const std::vector<SliderSpec> specs = slidersFromParams(kParams,
                                                                           {
                                                                           { "divide", "Divide", SliderFormat::Integer },
                                                                           { "offset", "Offset", SliderFormat::Integer },
                                                                           });
            return specs;
         }
      }

      ParamTable ClockDividerModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> ClockDividerModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> ClockDividerModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> ClockDividerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), clockDividerSpecs(), 140.0f);
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
