#include "BespokeWasm/WasmRuntimeState.h"

namespace bespoke::wasm
{
   WasmRuntimeState& runtimeState()
   {
      static WasmRuntimeState state;
      return state;
   }
}
