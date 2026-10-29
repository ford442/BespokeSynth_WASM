/**
 * BespokeSynth WASM - Scale singleton adapter implementation
 */

#include "BespokeWasm/adapters/ScaleModuleAdapter.h"
#include "BespokeWasm/modules/WasmModules.h"

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         // UI-only: the audio side never reads a Scale Params block.
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "root", .minValue = 0.0f, .maxValue = 11.0f, .defaultValue = 0.0f },
            { .id = 1, .name = "type", .minValue = 0.0f, .maxValue = 6.0f, .defaultValue = 0.0f },
         };
         static_assert(paramTableIdsAreIndices(kParams));
      }

      ParamTable ScaleModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::unique_ptr<Module> ScaleModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<ScaleModule>(id);
      }

      BESPOKE_REGISTER_MODULE(ScaleModuleAdapter);

   } // namespace wasm
} // namespace bespoke
