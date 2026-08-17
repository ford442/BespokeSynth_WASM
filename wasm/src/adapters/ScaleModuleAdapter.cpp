/**
 * BespokeSynth WASM - Scale singleton adapter implementation
 */

#include "BespokeWasm/adapters/ScaleModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"

namespace bespoke
{
   namespace wasm
   {
      std::vector<WasmControlDescriptor> ScaleModuleAdapter::controlDescriptors() const
      {
         return { { "root", 0.0f }, { "type", 0.0f } };
      }

      std::unique_ptr<Module> ScaleModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<ScaleModule>(id);
      }

      BESPOKE_REGISTER_MODULE(ScaleModuleAdapter);

   } // namespace wasm
} // namespace bespoke
