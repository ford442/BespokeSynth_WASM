/**
 * BespokeSynth WASM - Shared helpers for sample-reading adapters
 */

#include "BespokeWasm/SampleSource.h"
#include "BespokeWasm/BundledKit.h"

namespace bespoke
{
   namespace wasm
   {

      void bindSampleFromExtras(const WasmStringMap& extras, const SampleBuffer*& sample, SampleHandle& handle)
      {
         auto extra = extras.find(kSampleHashExtra);
         if (extra != extras.end() && !extra->second.empty())
            sample = findSampleOrBundled(extra->second);
         handle = sample ? SampleStore::instance().handleFor(sample->id()) : SampleHandle{};
      }

   } // namespace wasm
} // namespace bespoke
