/**
 * BespokeSynth WASM - Bundled drum kit
 *
 * A four-piece one-shot kit (kick, snare, hat, tom) ships in the resource pack under
 * /resource/kits/basic. Patches reference it like any other sample, by `sampleHash`
 * extras, so nothing about the saved-state format changes. The WAV is decoded the
 * first time a sampler asks for its hash, not at startup, so a patch that never uses
 * the kit pays nothing. scripts/gen_drum_kit.py regenerates the files; the test suite
 * checks the hashes below against the bytes on disk.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/SampleBuffer.h"
#include <string>

namespace bespoke
{
   namespace wasm
   {

      struct BundledKitSample
      {
         const char* name; // piece name, also the .wav basename
         const char* sha256; // over the file bytes, as SampleStore keys it
         int rootPitch; // the MIDI note a drum sequencer lane triggers it from
      };

      constexpr int kBundledKitSampleCount = 4;
      const BundledKitSample* bundledKitSamples();

      /** Directory the kit is read from (the preloaded resource pack). */
      const char* bundledKitDirectory();

      /**
       * SampleStore::findByHash, falling back to decoding the matching bundled kit file at
       * SampleStore::deviceSampleRate(). UI thread only; null when `hash` is neither stored
       * nor a kit sample, or the file can't be read.
       */
      const SampleBuffer* findSampleOrBundled(const std::string& hash);

   } // namespace wasm
} // namespace bespoke
