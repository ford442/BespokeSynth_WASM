/**
 * BespokeSynth WASM - Shared helpers for sample-reading adapters (sampler, granular, looper)
 *
 * A Params block that plays a SampleStore buffer carries the resolved pointer and the
 * SampleHandle naming it. These helpers do the four things every such adapter needs:
 * bind it from the patch's extras["sampleHash"] (UI thread), rebind it in the worklet's
 * heap, report it to the resource collector, and read interpolated frames on the audio
 * thread. The looper reads its own arena, not a SampleBuffer, but shares the readers.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/ResourceLifetime.h"
#include "BespokeWasm/SampleBuffer.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <cmath>

namespace bespoke
{
   namespace wasm
   {

      /** The extras key that names a module's sample by content hash. */
      constexpr const char* kSampleHashExtra = "sampleHash";

      /** UI thread: resolve extras["sampleHash"] (store or bundled kit) into `sample` / `handle`. */
      void bindSampleFromExtras(const WasmStringMap& extras, const SampleBuffer*& sample, SampleHandle& handle);

      /** Worklet instance: the UI copied the buffer under the same id and generation; find it in this heap. */
      inline void rebindSample(const SampleBuffer*& sample, const SampleHandle& handle)
      {
         sample = SampleStore::instance().resolve(handle);
      }

      inline void collectSample(const SampleBuffer* sample, const SampleHandle& handle, WasmResourceRefs& refs)
      {
         if (sample)
            refs.sampleIds.push_back(handle.index);
      }

      /** Linear interpolation at fractional frame `position`, clamped to the buffer. */
      inline float readLinear(const float* data, int frames, double position)
      {
         if (!data || frames <= 0)
            return 0.0f;
         if (position <= 0.0)
            return data[0];
         const int index = static_cast<int>(position);
         if (index >= frames - 1)
            return data[frames - 1];
         const float frac = static_cast<float>(position - index);
         return data[index] + (data[index + 1] - data[index]) * frac;
      }

      /** Like readLinear, but positions wrap around a loop of `frames` (granular, looper). */
      inline float readLinearWrapped(const float* data, int frames, double position)
      {
         if (!data || frames <= 0)
            return 0.0f;
         double wrapped = std::fmod(position, static_cast<double>(frames));
         if (wrapped < 0.0)
            wrapped += frames;
         const int index = static_cast<int>(wrapped);
         const int next = index + 1 < frames ? index + 1 : 0;
         const float frac = static_cast<float>(wrapped - index);
         return data[index] + (data[next] - data[index]) * frac;
      }

   } // namespace wasm
} // namespace bespoke
