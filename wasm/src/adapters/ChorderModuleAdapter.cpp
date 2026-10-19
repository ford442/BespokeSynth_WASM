/**
 * BespokeSynth WASM - Chorder (Note processor) adapter implementation
 */

#include "BespokeWasm/adapters/ChorderModuleAdapter.h"
#include "BespokeWasm/NoteProcessorUtil.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         struct ChordPreset
         {
            int count;
            int intervals[kChorderMaxTones];
         };

         // Index matches the "chord" choices; the last entry (Custom) is filled from the tone sliders.
         constexpr ChordPreset kPresets[kChorderCustomPreset] = {
            { 3, { 0, 4, 7, 0 } }, // Major
            { 3, { 0, 3, 7, 0 } }, // Minor
            { 3, { 0, 2, 7, 0 } }, // Sus2
            { 3, { 0, 5, 7, 0 } }, // Sus4
            { 3, { 0, 3, 6, 0 } }, // Dim
            { 3, { 0, 4, 8, 0 } }, // Aug
            { 4, { 0, 4, 7, 11 } }, // Maj7
            { 4, { 0, 3, 7, 10 } }, // Min7
            { 4, { 0, 4, 7, 10 } }, // Dom7
            { 3, { 0, 7, 12, 0 } }, // Power
            { 2, { 0, 12, 0, 0 } }, // Octave
         };

         const std::vector<SliderSpec>& chorderSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "chord", "Chord", 0.0f, static_cast<float>(kChorderCustomPreset), 0.0f, SliderFormat::Choice, { "Major", "Minor", "Sus2", "Sus4", "Dim", "Aug", "Maj7", "Min7", "Dom7", "Power", "Octave", "Custom" } },
               { "inversion", "Invert", 0.0f, 3.0f, 0.0f, SliderFormat::Integer },
               { "tone2", "Tone 2", 0.0f, 24.0f, 4.0f, SliderFormat::Integer },
               { "tone3", "Tone 3", 0.0f, 24.0f, 7.0f, SliderFormat::Integer },
               { "tone4", "Tone 4", 0.0f, 24.0f, 0.0f, SliderFormat::Integer },
               { "level", "Level", 0.0f, 1.0f, 1.0f, SliderFormat::Percent },
            };
            return specs;
         }

         void releaseHeld(ChorderRuntimeState& state, int pitch, NoteWriter& writer)
         {
            auto& held = state.held[pitch];
            for (int i = 0; i < held.count; ++i)
            {
               uint8_t& refs = state.refs[held.pitches[i]];
               if (refs > 0 && --refs == 0)
                  writer.push(held.pitches[i], 0.0f, false, held.channel);
            }
            held.count = 0;
         }
      }

      int ChorderModuleAdapter::chordTones(const ChorderParams& params, int* semitones, bool* isRoot)
      {
         int count = 0;
         if (params.chord >= kChorderCustomPreset)
         {
            semitones[count++] = 0;
            for (int tone : params.custom)
            {
               if (tone > 0 && count < kChorderMaxTones)
                  semitones[count++] = tone;
            }
         }
         else
         {
            const ChordPreset& preset = kPresets[std::max(0, params.chord)];
            for (int i = 0; i < preset.count; ++i)
               semitones[count++] = preset.intervals[i];
         }

         std::sort(semitones, semitones + count);
         for (int i = 0; i < count; ++i)
            isRoot[i] = semitones[i] == 0;

         // Inversion n lifts the lowest n tones an octave (the root keeps its role).
         const int lifts = std::max(0, std::min(params.inversion, count - 1));
         for (int i = 0; i < lifts; ++i)
            semitones[i] += 12;
         // Re-sort tone and root flag together.
         for (int i = 1; i < count; ++i)
         {
            for (int j = i; j > 0 && semitones[j] < semitones[j - 1]; --j)
            {
               std::swap(semitones[j], semitones[j - 1]);
               std::swap(isRoot[j], isRoot[j - 1]);
            }
         }
         return count;
      }

      std::vector<WasmControlDescriptor> ChorderModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(chorderSpecs());
      }

      std::vector<PortDescriptor> ChorderModuleAdapter::inputPorts() const
      {
         return { { PortType::Note, "Notes" } };
      }

      std::vector<PortDescriptor> ChorderModuleAdapter::outputPorts() const
      {
         return { { PortType::Note, "Chord" } };
      }

      std::unique_ptr<Module> ChorderModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), chorderSpecs());
      }

      void ChorderModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         const auto& specs = chorderSpecs();
         auto* params = new (dst) ChorderParams();
         params->chord = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "chord")));
         params->inversion = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "inversion")));
         params->custom[0] = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "tone2")));
         params->custom[1] = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "tone3")));
         params->custom[2] = static_cast<int>(std::lround(sliderSpecValue(specs, controls, "tone4")));
         params->level = sliderSpecValue(specs, controls, "level");
      }

      void ChorderModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) ChorderRuntimeState();
      }

      void ChorderModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<ChorderRuntimeState*>(runtimeState)->~ChorderRuntimeState();
      }

      void ChorderModuleAdapter::processNotes(void* runtimeState,
                                              const void* paramsPtr,
                                              const WasmNoteProcessBlock& block,
                                              WasmNoteEvent* outNotes,
                                              int maxNotes,
                                              int& outCount) const
      {
         NoteWriter writer(outNotes, maxNotes, outCount);
         if (!runtimeState || !paramsPtr || !writer.valid())
            return;

         auto& state = *static_cast<ChorderRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const ChorderParams*>(paramsPtr);

         int semitones[kChorderMaxTones];
         bool isRoot[kChorderMaxTones];
         const int toneCount = chordTones(params, semitones, isRoot);

         for (int n = 0; n < block.noteCount; ++n)
         {
            const WasmNoteEvent& in = block.notes[n];
            if (in.pitch < 0 || in.pitch > 127)
               continue;

            // A repeated note-on retriggers: release what this key held, then play the new chord.
            releaseHeld(state, in.pitch, writer);
            if (!in.isNoteOn)
               continue;

            auto& held = state.held[in.pitch];
            held.channel = static_cast<uint8_t>(in.channel & 0x0f);
            const float addedVelocity = std::max(0.0f, std::min(1.0f, in.velocity * params.level));
            for (int i = 0; i < toneCount; ++i)
            {
               const int pitch = in.pitch + semitones[i];
               if (pitch > 127)
                  continue;
               held.pitches[held.count++] = static_cast<uint8_t>(pitch);
               if (state.refs[pitch]++ == 0)
                  writer.push(pitch, isRoot[i] ? in.velocity : addedVelocity, true, in.channel);
            }
         }
      }

      BESPOKE_REGISTER_MODULE(ChorderModuleAdapter);

   } // namespace wasm
} // namespace bespoke
