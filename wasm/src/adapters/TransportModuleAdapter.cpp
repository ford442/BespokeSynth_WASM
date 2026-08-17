/**
 * BespokeSynth WASM - Transport singleton adapter implementation
 */

#include "BespokeWasm/adapters/TransportModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"

namespace bespoke
{
   namespace wasm
   {
      std::vector<WasmControlDescriptor> TransportModuleAdapter::controlDescriptors() const
      {
         return { { "bpm", 120.0f }, { "swing", 0.0f } };
      }

      std::unique_ptr<Module> TransportModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<TransportModule>(id);
      }

      BESPOKE_REGISTER_MODULE(TransportModuleAdapter);

   } // namespace wasm
} // namespace bespoke
