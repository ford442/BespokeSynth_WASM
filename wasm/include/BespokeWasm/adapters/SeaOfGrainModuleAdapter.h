/**
 * BespokeSynth WASM - Polyphonic granular synth adapter (sea of grain)
 *
 * The desktop Granulator kernel (Source/Granulator.cpp, not compiled: it schedules
 * grains on gTime) per note: each voice sprays overlapping Hann-windowed grains read
 * from a SampleStore buffer (by SampleHandle, through SampleSource.h). In `pitch` mode
 * a key sets the grain speed relative to `root`; in `position` mode it picks where in
 * the sample the grains come from, as the desktop keyboard mapping does
 * (`keyboard base pitch` + `keyboard num pitches`). Grains pan across `width`.
 * New modules start on the bundled pad (BundledKit.h) so they sound before a drop.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/PolyVoiceAdapter.h"
#include "BespokeWasm/ResourceLifetime.h"
#include "BespokeWasm/SampleBuffer.h"
#include "BespokeWasm/SampleStore.h"

namespace bespoke
{
   namespace wasm
   {

      struct SeaOfGrainParams
      {
         float volume = 0.8f;
         int mode = 1;
         float position = 0.3f;
         float overlap = 6.0f;
         float lengthMs = 80.0f;
         float positionRandomMs = 5.0f;
         float speedRandom = 0.0f;
         float spacingRandom = 1.0f;
         bool octaves = false;
         float width = 1.0f;
         float speed = 1.0f;
         int root = 60;
         int keyboardBase = 36;
         int keyboardRange = 24;
         float attack = 20.0f;
         float release = 400.0f;
         int voices = 8;
         const SampleBuffer* sample = nullptr;
         SampleHandle handle;
      };

      struct Grain
      {
         bool active = false;
         double position = 0.0;
         float speed = 1.0f;
         int age = 0;
         int length = 1;
         float volume = 1.0f;
         float pan = 0.0f;
      };

      struct SeaOfGrainVoice
      {
         static constexpr int kMaxGrains = 16;
         Grain grains[kMaxGrains]{};
         int nextGrain = 0;
         int spawnCountdown = 0;
         uint32_t rng = 1;
      };

      class SeaOfGrainModuleAdapter
      : public PolyVoiceAdapter<SeaOfGrainModuleAdapter, SeaOfGrainParams, SeaOfGrainVoice>
      {
      public:
         static constexpr bool kStereoVoices = true;

         const char* typeId() const override { return "seaofgrain"; }
         const char* displayName() const override { return "Sea of Grain"; }

         ParamTable paramTable() const override;
         std::span<const PortDescriptor> inputPorts() const override;
         std::span<const PortDescriptor> outputPorts() const override;
         std::unique_ptr<Module> createUiModule(int id) const override;

         void bindParams(int moduleId, const WasmStringMap& extras, void* params) const override;
         void rebindParams(int moduleId, void* params) const override;
         void collectResources(int moduleId, const void* params, WasmResourceRefs& refs) const override;

         static int voiceLimit(const SeaOfGrainParams& params) { return params.voices; }
         static dsp::EnvelopeSettings ampEnvelope(const SeaOfGrainParams& params);
         void startVoice(SeaOfGrainVoice& voice,
                         const PolyVoiceSlot& slot,
                         const SeaOfGrainParams& params,
                         float sampleRate) const;
         bool renderVoice(SeaOfGrainVoice& voice,
                          const PolyVoiceSlot& slot,
                          const SeaOfGrainParams& params,
                          float* const* out,
                          int count,
                          float sampleRate) const;

         /** Where (0..1) a voice's grains start: `position`, plus the key offset in position mode. */
         static float voicePosition(const SeaOfGrainParams& params, int pitch);
      };

   } // namespace wasm
} // namespace bespoke
