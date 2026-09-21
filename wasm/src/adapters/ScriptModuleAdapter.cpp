/**
 * BespokeSynth WASM - Script module adapter implementation
 */

#include "BespokeWasm/adapters/ScriptModuleAdapter.h"
#include "BespokeWasm/ScriptNoteQueue.h"
#include "BespokeWasm/modules/WasmModules.h"
#include <new>

namespace bespoke
{
   namespace wasm
   {
      std::vector<PortDescriptor> ScriptModuleAdapter::outputPorts() const
      {
         return { { PortType::Note, "Notes" } };
      }

      std::unique_ptr<Module> ScriptModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<ScriptModule>(id);
      }

      void ScriptModuleAdapter::fillParams(int moduleId,
                                           const WasmControlMap& controls,
                                           const WasmStringMap& extras,
                                           void* dst) const
      {
         (void)controls;
         (void)extras; // the script text never reaches the audio side
         auto* params = new (dst) ScriptParams();
         params->moduleId = moduleId;
      }

      void ScriptModuleAdapter::emitNotesForBeatRange(void* runtimeState,
                                                      const void* paramsPtr,
                                                      double beatStart,
                                                      double beatEnd,
                                                      WasmNoteEvent* outNotes,
                                                      int maxNotes,
                                                      int& outCount,
                                                      const WasmPulseEvent* pulses,
                                                      int pulseCount) const
      {
         (void)runtimeState;
         (void)beatStart;
         (void)beatEnd;
         (void)pulses;
         (void)pulseCount;
         outCount = 0;
         if (!paramsPtr || !outNotes)
            return;
         const auto& params = *static_cast<const ScriptParams*>(paramsPtr);
         outCount = drainScriptNotes(params.moduleId, outNotes, maxNotes);
      }

      BESPOKE_REGISTER_MODULE(ScriptModuleAdapter);

   } // namespace wasm
} // namespace bespoke
