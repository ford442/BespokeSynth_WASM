/**
 * BespokeSynth WASM - AudioGraphSnapshot wire format
 *
 * The UI instance (canvas, GPU) and the AudioWorklet instance (DSP only) are two
 * WASM modules with separate heaps. A published snapshot crosses between them as
 * bytes: the POD parts are copied as-is (both modules are built from the same
 * sources) and adapter type indices travel as type ids, so the two registries
 * never have to agree on registration order. Pointers inside parameter blocks
 * are re-resolved on the receiving side by WasmModuleAdapter::rebindParams.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {
      /** Bumped whenever the encoded layout changes. Both modules must agree. */
      constexpr uint32_t kAudioGraphWireVersion = 3;

      /**
       * Version plus the sizes of every struct copied verbatim, so a stale cached
       * audio module is rejected instead of misreading a newer UI module's bytes.
       */
      uint32_t audioGraphWireAbi();

      /** UI side: append `graph` to `out` (cleared first). Allocates; never call from audio. */
      void encodeAudioGraphSnapshot(const AudioGraphSnapshot& graph, std::vector<uint8_t>& out);

      /**
       * Audio-instance side: rebuild a snapshot from `encodeAudioGraphSnapshot` bytes. Type
       * ids are mapped onto this module's registry and every node's params are rebound
       * (rebindParams). Returns false, with `error` set, on a version/ABI mismatch, a
       * truncated buffer, or a params block whose size this build disagrees with.
       */
      bool decodeAudioGraphSnapshot(const uint8_t* bytes,
                                    size_t length,
                                    AudioGraphSnapshot& graph,
                                    std::string& error);

   } // namespace wasm
} // namespace bespoke
