#include "BespokeWasm/WasmRuntimeState.h"
#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/AudioRemote.h"
#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/SampleStore.h"

namespace bespoke::wasm
{
   WasmRuntimeState& runtimeState()
   {
      static WasmRuntimeState state;
      return state;
   }

   int currentDeviceSampleRate()
   {
      const auto& state = runtimeState();
      if (state.externalAudioActive && state.externalSampleRate > 0.0f)
         return static_cast<int>(state.externalSampleRate);
      if (state.audioBackend)
         return state.audioBackend->getSampleRate();
      return state.externalSampleRate > 0.0f ? static_cast<int>(state.externalSampleRate) : 44100;
   }

   void syncSampleStoreRate()
   {
      SampleStore::instance().setDeviceSampleRate(currentDeviceSampleRate());
   }

   void resetAudioGraphEngineForPatch()
   {
      auto& state = runtimeState();
      if (state.remoteAudioActive)
      {
         // The worklet engine resets its clock and state when the new graph reaches it.
         notifyAudioGraphReset();
         return;
      }
      if (!state.audioGraphEngine)
      {
         // Same sizing as bespoke_init; reached only when a patch loads before the engine exists.
         state.audioGraphEngine = std::make_unique<AudioGraphEngine>();
         state.audioGraphEngine->prepareForBlock(2048, 128, 32);
      }
      else
      {
         state.audioGraphEngine->resetClock();
      }
      if (state.canvas)
      {
         if (const auto graph = state.canvas->getAudioGraphSnapshot())
            state.audioGraphEngine->prepareRuntimeStates(*graph);
      }
   }
}
