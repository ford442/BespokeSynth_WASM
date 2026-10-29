/**
 * BespokeSynth WASM - Script module adapter implementation
 */

#include "BespokeWasm/adapters/ScriptModuleAdapter.h"
#include "BespokeWasm/ScriptNoteQueue.h"
#include "BespokeWasm/modules/WasmModules.h"

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr PortDescriptor kOutputs[] = { { PortType::Note, "Notes" } };
      }

      std::span<const PortDescriptor> ScriptModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> ScriptModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<ScriptModule>(id);
      }

      void ScriptModuleAdapter::bindParams(int moduleId, const WasmStringMap& extras, void* params) const
      {
         (void)extras; // the script text never reaches the audio side
         paramsOf(params).moduleId = moduleId;
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
