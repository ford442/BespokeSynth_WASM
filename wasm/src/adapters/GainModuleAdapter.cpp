/**
 * BespokeSynth WASM - Gain adapter implementation
 */

#include "BespokeWasm/adapters/GainModuleAdapter.h"
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

      std::vector<WasmControlDescriptor> GainModuleAdapter::controlDescriptors() const
      {
         return { { "gain", 0.7f } };
      }

      std::vector<PortDescriptor> GainModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" }, { PortType::Modulation, "Mod" } };
      }

      std::vector<PortDescriptor> GainModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> GainModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<GainModule>(id);
      }

      void GainModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) GainParams();
         params->gain = wasmControlValue(controls, "gain", 0.7f);
      }

      void GainModuleAdapter::processAudio(void* runtimeState,
                                           const void* paramsPtr,
                                           float* buffer,
                                           const WasmAudioProcessContext& context) const
      {
         (void)runtimeState;
         if (!paramsPtr || !buffer)
            return;

         const auto& params = *static_cast<const GainParams*>(paramsPtr);
         const float gain = clampFloat(params.gain, 0.0f, 1.0f);
         for (int i = 0; i < context.numSamples; ++i)
            buffer[i] *= gain;
      }

      BESPOKE_REGISTER_MODULE(GainModuleAdapter);

   } // namespace wasm
} // namespace bespoke
