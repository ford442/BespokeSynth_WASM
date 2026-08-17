/**
 * BespokeSynth WASM - Output sink adapter implementation
 */

#include "BespokeWasm/adapters/OutputModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"

namespace bespoke
{
   namespace wasm
   {
      std::vector<WasmControlDescriptor> OutputModuleAdapter::controlDescriptors() const
      {
         return { { "level", 0.0f } };
      }

      std::vector<PortDescriptor> OutputModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" } };
      }

      std::vector<PortDescriptor> OutputModuleAdapter::outputPorts() const
      {
         return {};
      }

      std::unique_ptr<Module> OutputModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<OutputModule>(id);
      }

      BESPOKE_REGISTER_MODULE(OutputModuleAdapter);

   } // namespace wasm
} // namespace bespoke
