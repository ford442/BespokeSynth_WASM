/**
 * BespokeSynth WASM - Polyphonic voice adapter base (FM, Karplus-Strong, granular)
 *
 * Owns the voice pool: note-on allocation and stealing, pitch from the graph Scale
 * (GraphMusicScale::noteFrequency via context.noteFrequencies) plus pitch bend, a
 * per-voice amp envelope, and mixing. A derived adapter only says how one voice
 * starts and renders:
 *
 *   class FooAdapter : public PolyVoiceAdapter<FooAdapter, FooParams, FooVoice>
 *   {
 *      // Polyphony and amp envelope for this block.
 *      static int voiceLimit(const FooParams&);
 *      static dsp::EnvelopeSettings ampEnvelope(const FooParams&);
 *      // Note-on (a new voice or a stolen one); `slot` has pitch, frequency, velocity.
 *      void startVoice(FooVoice&, const PolyVoiceSlot& slot, const FooParams&, float sampleRate) const;
 *      // Fill out[0] (and out[1] for kStereoVoices) with `count` samples, before the amp envelope.
 *      // Return false once the voice has died out on its own (a plucked string).
 *      bool renderVoice(FooVoice&, const PolyVoiceSlot& slot, const FooParams&, float* const* out, int count,
 *                       float sampleRate) const;
 *   };
 *
 * Optional: `static constexpr bool kStereoVoices = true;` (granular spreads grains),
 * `void releaseVoice(FooVoice&, const PolyVoiceSlot&, const FooParams&) const` (note-off hook) and
 * `static float ampPeak(const FooParams&, float velocity)` (default: the velocity).
 * Everything runs on the audio thread without allocating.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/TuningTable.h"
#include "BespokeWasm/dsp/Envelope.h"
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstring>

namespace bespoke
{
   namespace wasm
   {

      constexpr int kPolyMaxVoices = 8;

      /** What the base tracks per voice; the derived voice DSP lives next to it. */
      struct PolyVoiceSlot
      {
         bool active = false; // rendering (gated or releasing)
         bool gated = false; // key held
         int pitch = 60;
         int channel = 0;
         float velocity = 1.0f;
         float bend = 0.0f; // semitones
         float frequency = 440.0f; // tuned pitch with bend
         uint32_t age = 0;
         dsp::Envelope amp;
      };

      template <typename TVoice>
      struct PolyVoiceState
      {
         PolyVoiceSlot slots[kPolyMaxVoices]{};
         TVoice voices[kPolyMaxVoices]{};
         uint32_t ageCounter = 0;
         /** Note-ons that took a voice still held by another key (tests, voice-stealing e2e). */
         uint32_t steals = 0;
      };

      namespace polyvoice
      {
         template <typename T>
         concept HasStereoVoices = requires { T::kStereoVoices; };

         template <typename T>
         constexpr bool stereoVoices()
         {
            if constexpr (HasStereoVoices<T>)
               return T::kStereoVoices;
            else
               return false;
         }

         template <typename TDerived, typename TParams>
         concept HasAmpPeak = requires(const TParams& p) {
            { TDerived::ampPeak(p, 1.0f) } -> std::convertible_to<float>;
         };

         template <typename TDerived, typename TVoice, typename TParams>
         concept HasReleaseHook = requires(const TDerived& d, TVoice& v, const PolyVoiceSlot& s, const TParams& p) {
            d.releaseVoice(v, s, p);
         };
      }

      template <typename TDerived, typename TParams, typename TVoice>
      class PolyVoiceAdapter : public AdapterStorage<TParams, PolyVoiceState<TVoice>>
      {
      public:
         using State = PolyVoiceState<TVoice>;
         static constexpr int kChunk = 64;

         ModuleCategory category() const override { return ModuleCategory::Synth; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::AudioSource; }

         int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const override
         {
            (void)params;
            (void)context;
            return polyvoice::stereoVoices<TDerived>() ? 2 : 1;
         }

         /** Voices currently rendering (gated or in release). */
         static int activeVoiceCount(const void* runtimeState)
         {
            const auto& state = *static_cast<const State*>(runtimeState);
            int count = 0;
            for (const auto& slot : state.slots)
               count += slot.active ? 1 : 0;
            return count;
         }

         static uint32_t stealCount(const void* runtimeState) { return static_cast<const State*>(runtimeState)->steals; }

         void processAudio(void* runtimeState,
                           const void* paramsPtr,
                           float* const* channels,
                           const WasmAudioProcessContext& context) const override
         {
            if (!runtimeState || !paramsPtr || !channels || !channels[0])
               return;
            auto& state = *static_cast<State*>(runtimeState);
            const auto& params = *static_cast<const TParams*>(paramsPtr);
            const TDerived& self = static_cast<const TDerived&>(*this);
            const int limit = std::max(1, std::min(kPolyMaxVoices, TDerived::voiceLimit(params)));
            const float sampleRate = std::max(1.0f, context.sampleRate);

            for (int n = 0; n < context.noteCount; ++n)
               handleNote(state, params, context.notes[n], limit, context.noteFrequencies, sampleRate);

            const int outChannels = context.numChannels;
            for (int ch = 0; ch < outChannels; ++ch)
               std::fill(channels[ch], channels[ch] + context.numSamples, 0.0f);

            const dsp::EnvelopeSettings envelope = TDerived::ampEnvelope(params);
            constexpr bool kStereo = polyvoice::stereoVoices<TDerived>();
            float scratch[2][kChunk];
            float* voiceOut[2] = { scratch[0], kStereo ? scratch[1] : nullptr };
            for (int v = 0; v < kPolyMaxVoices; ++v)
            {
               PolyVoiceSlot& slot = state.slots[v];
               if (!slot.active)
                  continue;
               slot.amp.prepare(envelope, sampleRate);
               for (int start = 0; start < context.numSamples && slot.active; start += kChunk)
               {
                  const int count = std::min(kChunk, context.numSamples - start);
                  const bool alive = self.renderVoice(state.voices[v], slot, params, voiceOut, count, sampleRate);
                  for (int i = 0; i < count; ++i)
                  {
                     const float gain = slot.amp.next();
                     scratch[0][i] *= gain;
                     if constexpr (kStereo)
                        scratch[1][i] *= gain;
                  }
                  if constexpr (kStereo)
                  {
                     if (outChannels == 2)
                     {
                        dsp::accumulateScaled(channels[0] + start, scratch[0], count, 1.0f);
                        dsp::accumulateScaled(channels[1] + start, scratch[1], count, 1.0f);
                     }
                     else
                     {
                        dsp::accumulateScaled(channels[0] + start, scratch[0], count, 0.5f);
                        dsp::accumulateScaled(channels[0] + start, scratch[1], count, 0.5f);
                     }
                  }
                  else
                  {
                     for (int ch = 0; ch < outChannels; ++ch)
                        dsp::accumulateScaled(channels[ch] + start, scratch[0], count, 1.0f);
                  }
                  if (!alive || slot.amp.idle())
                  {
                     slot.active = false;
                     slot.gated = false;
                     slot.amp.reset();
                  }
               }
            }
         }

      private:
         void handleNote(State& state,
                         const TParams& params,
                         const WasmNoteEvent& note,
                         int limit,
                         const float* noteFrequencies,
                         float sampleRate) const
         {
            const TDerived& self = static_cast<const TDerived&>(*this);
            if (note.pitch < 0 || note.pitch > 127)
               return;
            if (isPitchBendEvent(note))
            {
               for (auto& slot : state.slots)
               {
                  if (slot.active && slot.pitch == note.pitch)
                  {
                     slot.bend = note.pitchBend;
                     slot.frequency = noteFrequency(noteFrequencies, slot.pitch) * pitchBendRatio(slot.bend);
                  }
               }
               return;
            }
            if (!note.isNoteOn)
            {
               for (int v = 0; v < kPolyMaxVoices; ++v)
               {
                  PolyVoiceSlot& slot = state.slots[v];
                  if (!slot.gated || slot.pitch != note.pitch)
                     continue;
                  slot.gated = false;
                  slot.amp.stop();
                  if constexpr (polyvoice::HasReleaseHook<TDerived, TVoice, TParams>)
                     self.releaseVoice(state.voices[v], slot, params);
               }
               return;
            }

            const int index = pickVoice(state, note.pitch, limit);
            PolyVoiceSlot& slot = state.slots[index];
            if (slot.gated && slot.pitch != note.pitch)
               ++state.steals;
            slot.active = true;
            slot.gated = true;
            slot.pitch = note.pitch;
            slot.channel = note.channel;
            slot.velocity = std::max(0.0f, std::min(1.0f, note.velocity));
            slot.bend = note.pitchBend;
            slot.frequency = noteFrequency(noteFrequencies, note.pitch) * pitchBendRatio(note.pitchBend);
            slot.age = ++state.ageCounter;
            if constexpr (polyvoice::HasAmpPeak<TDerived, TParams>)
               slot.amp.start(TDerived::ampPeak(params, slot.velocity));
            else
               slot.amp.start(slot.velocity);
            self.startVoice(state.voices[index], slot, params, sampleRate);
         }

         /**
          * Same key held: retrigger it. Else a free voice, else the oldest released one, else
          * the oldest held one. Voices past `limit` are released so a lowered limit takes effect.
          */
         static int pickVoice(State& state, int pitch, int limit)
         {
            for (int v = limit; v < kPolyMaxVoices; ++v)
            {
               if (state.slots[v].gated)
               {
                  state.slots[v].gated = false;
                  state.slots[v].amp.stop();
               }
            }
            for (int v = 0; v < limit; ++v)
            {
               if (state.slots[v].gated && state.slots[v].pitch == pitch)
                  return v;
            }
            int freeVoice = -1;
            int oldestReleased = -1;
            int oldestHeld = 0;
            for (int v = 0; v < limit; ++v)
            {
               const PolyVoiceSlot& slot = state.slots[v];
               if (!slot.active)
               {
                  freeVoice = v;
                  break;
               }
               if (!slot.gated && (oldestReleased < 0 || slot.age < state.slots[oldestReleased].age))
                  oldestReleased = v;
               if (slot.gated && slot.age < state.slots[oldestHeld].age)
                  oldestHeld = v;
            }
            if (freeVoice >= 0)
               return freeVoice;
            return oldestReleased >= 0 ? oldestReleased : oldestHeld;
         }
      };

   } // namespace wasm
} // namespace bespoke
