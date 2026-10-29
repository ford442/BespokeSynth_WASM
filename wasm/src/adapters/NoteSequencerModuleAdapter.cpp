/**
 * BespokeSynth WASM - Note step sequencer adapter implementation
 */

#include "BespokeWasm/adapters/NoteSequencerModuleAdapter.h"
#include "BespokeWasm/NoteProcessorUtil.h"
#include "BespokeWasm/ScaleQuantize.h"
#include "BespokeWasm/modules/NoteSequencerModule.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         // clang-format off
         // Per-step rows are generated (tone0..15, vel0..15, len0..15); ids stay stable for saved patches.
         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "steps", .minValue = 1.0f, .maxValue = 16.0f, .defaultValue = 8.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteSequencerParams, steps), .flags = kParamClamp },
            { .id = 1, .name = "interval", .minValue = 0.0f, .maxValue = 3.0f, .defaultValue = 2.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteSequencerParams, interval), .flags = kParamClamp },
            { .id = 2, .name = "octave", .minValue = 0.0f, .maxValue = 8.0f, .defaultValue = 4.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteSequencerParams, octave), .flags = kParamClamp },
            { .id = 3, .name = "rows", .minValue = 4.0f, .maxValue = 16.0f, .defaultValue = 8.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteSequencerParams, rows), .flags = kParamClamp },
            { .id = 4, .name = "mode", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .kind = ParamKind::IntRound, .offset = offsetof(NoteSequencerParams, mode), .flags = kParamClamp },
            { .id = 5, .name = "tone0", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, tone) + 0 * sizeof(float), .flags = kParamClamp },
            { .id = 6, .name = "tone1", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 2.0f, .offset = offsetof(NoteSequencerParams, tone) + 1 * sizeof(float), .flags = kParamClamp },
            { .id = 7, .name = "tone2", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 4.0f, .offset = offsetof(NoteSequencerParams, tone) + 2 * sizeof(float), .flags = kParamClamp },
            { .id = 8, .name = "tone3", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 7.0f, .offset = offsetof(NoteSequencerParams, tone) + 3 * sizeof(float), .flags = kParamClamp },
            { .id = 9, .name = "tone4", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 4.0f, .offset = offsetof(NoteSequencerParams, tone) + 4 * sizeof(float), .flags = kParamClamp },
            { .id = 10, .name = "tone5", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 2.0f, .offset = offsetof(NoteSequencerParams, tone) + 5 * sizeof(float), .flags = kParamClamp },
            { .id = 11, .name = "tone6", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 5.0f, .offset = offsetof(NoteSequencerParams, tone) + 6 * sizeof(float), .flags = kParamClamp },
            { .id = 12, .name = "tone7", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 3.0f, .offset = offsetof(NoteSequencerParams, tone) + 7 * sizeof(float), .flags = kParamClamp },
            { .id = 13, .name = "tone8", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, tone) + 8 * sizeof(float), .flags = kParamClamp },
            { .id = 14, .name = "tone9", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, tone) + 9 * sizeof(float), .flags = kParamClamp },
            { .id = 15, .name = "tone10", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, tone) + 10 * sizeof(float), .flags = kParamClamp },
            { .id = 16, .name = "tone11", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, tone) + 11 * sizeof(float), .flags = kParamClamp },
            { .id = 17, .name = "tone12", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, tone) + 12 * sizeof(float), .flags = kParamClamp },
            { .id = 18, .name = "tone13", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, tone) + 13 * sizeof(float), .flags = kParamClamp },
            { .id = 19, .name = "tone14", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, tone) + 14 * sizeof(float), .flags = kParamClamp },
            { .id = 20, .name = "tone15", .minValue = 0.0f, .maxValue = 15.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, tone) + 15 * sizeof(float), .flags = kParamClamp },
            { .id = 21, .name = "vel0", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 0 * sizeof(float), .flags = kParamClamp },
            { .id = 22, .name = "vel1", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.6f, .offset = offsetof(NoteSequencerParams, velocity) + 1 * sizeof(float), .flags = kParamClamp },
            { .id = 23, .name = "vel2", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 2 * sizeof(float), .flags = kParamClamp },
            { .id = 24, .name = "vel3", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.6f, .offset = offsetof(NoteSequencerParams, velocity) + 3 * sizeof(float), .flags = kParamClamp },
            { .id = 25, .name = "vel4", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 4 * sizeof(float), .flags = kParamClamp },
            { .id = 26, .name = "vel5", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .offset = offsetof(NoteSequencerParams, velocity) + 5 * sizeof(float), .flags = kParamClamp },
            { .id = 27, .name = "vel6", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.7f, .offset = offsetof(NoteSequencerParams, velocity) + 6 * sizeof(float), .flags = kParamClamp },
            { .id = 28, .name = "vel7", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.6f, .offset = offsetof(NoteSequencerParams, velocity) + 7 * sizeof(float), .flags = kParamClamp },
            { .id = 29, .name = "vel8", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 8 * sizeof(float), .flags = kParamClamp },
            { .id = 30, .name = "vel9", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 9 * sizeof(float), .flags = kParamClamp },
            { .id = 31, .name = "vel10", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 10 * sizeof(float), .flags = kParamClamp },
            { .id = 32, .name = "vel11", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 11 * sizeof(float), .flags = kParamClamp },
            { .id = 33, .name = "vel12", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 12 * sizeof(float), .flags = kParamClamp },
            { .id = 34, .name = "vel13", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 13 * sizeof(float), .flags = kParamClamp },
            { .id = 35, .name = "vel14", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 14 * sizeof(float), .flags = kParamClamp },
            { .id = 36, .name = "vel15", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.8f, .offset = offsetof(NoteSequencerParams, velocity) + 15 * sizeof(float), .flags = kParamClamp },
            { .id = 37, .name = "len0", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 0 * sizeof(float), .flags = kParamClamp },
            { .id = 38, .name = "len1", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 1 * sizeof(float), .flags = kParamClamp },
            { .id = 39, .name = "len2", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 2 * sizeof(float), .flags = kParamClamp },
            { .id = 40, .name = "len3", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 3 * sizeof(float), .flags = kParamClamp },
            { .id = 41, .name = "len4", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 4 * sizeof(float), .flags = kParamClamp },
            { .id = 42, .name = "len5", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 5 * sizeof(float), .flags = kParamClamp },
            { .id = 43, .name = "len6", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 6 * sizeof(float), .flags = kParamClamp },
            { .id = 44, .name = "len7", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 7 * sizeof(float), .flags = kParamClamp },
            { .id = 45, .name = "len8", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 8 * sizeof(float), .flags = kParamClamp },
            { .id = 46, .name = "len9", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 9 * sizeof(float), .flags = kParamClamp },
            { .id = 47, .name = "len10", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 10 * sizeof(float), .flags = kParamClamp },
            { .id = 48, .name = "len11", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 11 * sizeof(float), .flags = kParamClamp },
            { .id = 49, .name = "len12", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 12 * sizeof(float), .flags = kParamClamp },
            { .id = 50, .name = "len13", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 13 * sizeof(float), .flags = kParamClamp },
            { .id = 51, .name = "len14", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 14 * sizeof(float), .flags = kParamClamp },
            { .id = 52, .name = "len15", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(NoteSequencerParams, length) + 15 * sizeof(float), .flags = kParamClamp },
         };
         // clang-format on
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kOutputs[] = { { PortType::Note, "Notes" } };
         constexpr double kStepBeats[] = { 1.0, 0.5, 0.25, 0.125 };
         constexpr double kBeatEpsilon = 1e-9;
         // A length of 0 still sounds a short blip rather than a zero-length note.
         constexpr float kMinLength = 0.05f;
      }

      double NoteSequencerModuleAdapter::stepBeats(int interval)
      {
         return kStepBeats[std::max(0, std::min(3, interval))];
      }

      int NoteSequencerModuleAdapter::rowPitch(const NoteSequencerParams& params, int tone, const GraphMusicScale* scale)
      {
         const int root = scale ? ((scale->root % 12) + 12) % 12 : 0;
         const int base = 12 * (params.octave + 1) + root;
         if (params.mode == 1 || !scale)
            return clampMidiPitch(base + tone);
         return transposeByScaleDegrees(base, tone, *scale);
      }

      ParamTable NoteSequencerModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> NoteSequencerModuleAdapter::inputPorts() const
      {
         return {};
      }

      std::span<const PortDescriptor> NoteSequencerModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> NoteSequencerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<NoteSequencerModule>(id, kParams);
      }

      void NoteSequencerModuleAdapter::processNotes(void* runtimeState,
                                                    const void* paramsPtr,
                                                    const WasmNoteProcessBlock& block,
                                                    WasmNoteEvent* outNotes,
                                                    int maxNotes,
                                                    int& outCount) const
      {
         NoteWriter writer(outNotes, maxNotes, outCount);
         if (!runtimeState || !paramsPtr || !writer.valid())
            return;
         auto& state = *static_cast<NoteSequencerRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const NoteSequencerParams*>(paramsPtr);
         const double step = stepBeats(params.interval);
         const int steps = std::max(1, std::min(kNoteSeqMaxSteps, params.steps));
         const int rows = std::max(1, params.rows);

         auto stopSounding = [&]()
         {
            if (!state.sounding)
               return;
            writer.push(state.soundingPitch, 0.0f, false);
            state.sounding = false;
            state.noteOffBeat = -1.0;
         };
         auto releaseDue = [&](double limitBeat)
         {
            if (state.sounding && state.noteOffBeat >= 0.0 && state.noteOffBeat <= limitBeat + kBeatEpsilon)
               stopSounding();
         };

         // The transport jumped back (stop, loop, seek): restart the pattern from the new position.
         if (state.lastStep >= 0 && block.beatStart < static_cast<double>(state.lastStep) * step - kBeatEpsilon)
         {
            stopSounding();
            state.lastStep = -1;
         }

         const long long firstStep = static_cast<long long>(std::ceil(block.beatStart / step - kBeatEpsilon));
         for (long long k = firstStep;; ++k)
         {
            const double stepBeat = static_cast<double>(k) * step;
            if (stepBeat >= block.beatEnd - kBeatEpsilon)
               break;
            if (k <= state.lastStep)
               continue;
            state.lastStep = k;
            releaseDue(stepBeat);
            stopSounding();

            const int index = static_cast<int>(((k % steps) + steps) % steps);
            const float velocity = params.velocity[index];
            if (velocity <= 0.0f)
               continue;
            const int tone = std::max(0, std::min(rows - 1, static_cast<int>(std::lround(params.tone[index]))));
            const int pitch = rowPitch(params, tone, block.scale);
            writer.push(pitch, velocity, true);
            state.sounding = true;
            state.soundingPitch = pitch;
            state.noteOffBeat = stepBeat + step * std::max(kMinLength, std::min(1.0f, params.length[index]));
         }
         releaseDue(block.beatEnd);
      }

      BESPOKE_REGISTER_MODULE(NoteSequencerModuleAdapter);

   } // namespace wasm
} // namespace bespoke
