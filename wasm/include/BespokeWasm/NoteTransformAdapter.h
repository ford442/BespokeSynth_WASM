/**
 * BespokeSynth WASM - Note-transform adapter base (capo, note chance, pitch bender)
 *
 * One Note in, one Note out, at most one output note per held key. The base remembers
 * which output pitch each input key turned into, so the note-off (and any pitch bend)
 * reaches the note that is actually sounding even after a control changed in between.
 * A derived adapter decides what a note-on becomes:
 *
 *   class FooAdapter : public NoteTransformAdapter<FooAdapter, FooParams, FooExtra>
 *   {
 *      // `pitch` / `velocity` start as the input's; return false to drop the note.
 *      bool transformNoteOn(FooExtra&, const FooParams&, const WasmNoteProcessBlock&, const WasmNoteEvent& in,
 *                           int& pitch, float& velocity) const;
 *   };
 *
 * Optional hooks:
 *   float transformBend(const FooExtra&, const FooParams&, float inputBend) const;  // default: as is
 *   void beginBlock(FooExtra&, const FooParams&, const WasmNoteProcessBlock&, NoteTransformHeld&, NoteWriter&) const;
 *     (runs before the block's notes: a retrigger on a capo change, a bend update)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AdapterStorage.h"
#include "BespokeWasm/NoteProcessorUtil.h"
#include <algorithm>
#include <concepts>
#include <cstdint>

namespace bespoke
{
   namespace wasm
   {

      /** Per input key: the output pitch it sounds as (-1 = silent), and what it arrived with. */
      struct NoteTransformHeld
      {
         int8_t out[128];
         uint8_t channel[128]{};
         float velocity[128]{};
         float inputBend[128]{};

         NoteTransformHeld() { std::fill(out, out + 128, static_cast<int8_t>(-1)); }

         template <typename Fn>
         void forEachSounding(Fn&& fn) const
         {
            for (int key = 0; key < 128; ++key)
            {
               if (out[key] >= 0)
                  fn(key, static_cast<int>(out[key]));
            }
         }
      };

      template <typename TExtra>
      struct NoteTransformState
      {
         NoteTransformHeld held;
         TExtra extra{};
      };

      namespace notetransform
      {
         template <typename TDerived, typename TExtra, typename TParams>
         concept HasBendHook = requires(const TDerived& d, const TExtra& e, const TParams& p) {
            { d.transformBend(e, p, 0.0f) } -> std::convertible_to<float>;
         };

         template <typename TDerived, typename TExtra, typename TParams>
         concept HasBeginBlock = requires(const TDerived& d, TExtra& e, const TParams& p, const WasmNoteProcessBlock& b,
                                          NoteTransformHeld& h, NoteWriter& w) { d.beginBlock(e, p, b, h, w); };
      }

      template <typename TDerived, typename TParams, typename TExtra = NoAdapterStorage>
      class NoteTransformAdapter : public AdapterStorage<TParams, NoteTransformState<TExtra>>
      {
      public:
         using State = NoteTransformState<TExtra>;

         ModuleCategory category() const override { return ModuleCategory::NoteEffect; }
         WasmAudioRole audioRole() const override { return WasmAudioRole::NoteProcessor; }
         // Input notes were already snapped to the Scale on the way in; the transform is exact.
         bool quantizesNotesToScale() const override { return false; }

         void processNotes(void* runtimeState,
                           const void* paramsPtr,
                           const WasmNoteProcessBlock& block,
                           WasmNoteEvent* outNotes,
                           int maxNotes,
                           int& outCount) const override
         {
            NoteWriter writer(outNotes, maxNotes, outCount);
            if (!runtimeState || !paramsPtr || !writer.valid())
               return;
            auto& state = *static_cast<State*>(runtimeState);
            const auto& params = *static_cast<const TParams*>(paramsPtr);
            const TDerived& self = static_cast<const TDerived&>(*this);
            NoteTransformHeld& held = state.held;

            if constexpr (notetransform::HasBeginBlock<TDerived, TExtra, TParams>)
               self.beginBlock(state.extra, params, block, held, writer);

            for (int n = 0; n < block.noteCount; ++n)
            {
               const WasmNoteEvent& in = block.notes[n];
               if (in.pitch < 0 || in.pitch > 127)
                  continue;
               int8_t& sounding = held.out[in.pitch];
               if (isPitchBendEvent(in))
               {
                  held.inputBend[in.pitch] = in.pitchBend;
                  if (sounding >= 0)
                     writer.pushBend(sounding, bendOf(state.extra, params, in.pitchBend), held.channel[in.pitch]);
                  continue;
               }
               // A repeated note-on retriggers: end what this key held first.
               if (sounding >= 0)
               {
                  writer.push(sounding, 0.0f, false, held.channel[in.pitch]);
                  sounding = -1;
               }
               if (!in.isNoteOn)
                  continue;

               int pitch = in.pitch;
               float velocity = in.velocity;
               if (!self.transformNoteOn(state.extra, params, block, in, pitch, velocity))
                  continue;
               pitch = clampMidiPitch(pitch);
               sounding = static_cast<int8_t>(pitch);
               held.channel[in.pitch] = static_cast<uint8_t>(in.channel & 0x0f);
               held.velocity[in.pitch] = in.velocity;
               held.inputBend[in.pitch] = in.pitchBend;
               writer.push(pitch, velocity, true, in.channel, 0, bendOf(state.extra, params, in.pitchBend));
            }
         }

      protected:
         float bendOf(const TExtra& extra, const TParams& params, float inputBend) const
         {
            if constexpr (notetransform::HasBendHook<TDerived, TExtra, TParams>)
               return static_cast<const TDerived&>(*this).transformBend(extra, params, inputBend);
            else
               return inputBend;
         }
      };

   } // namespace wasm
} // namespace bespoke
