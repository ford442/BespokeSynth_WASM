/**
 * BespokeSynth WASM - Arpeggiator (Note processor) adapter implementation
 */

#include "BespokeWasm/adapters/ArpeggiatorModuleAdapter.h"
#include "BespokeWasm/NoteProcessorUtil.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr int kMaxOctaves = 4;
         constexpr double kBeatEpsilon = 1e-9;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "rate", .minValue = 0.0f, .maxValue = 3.0f, .defaultValue = 2.0f, .kind = ParamKind::IntRound, .offset = offsetof(ArpeggiatorParams, rate), .flags = kParamClamp },
            { .id = 1, .name = "mode", .minValue = 0.0f, .maxValue = 4.0f, .defaultValue = 0.0f, .kind = ParamKind::IntRound, .offset = offsetof(ArpeggiatorParams, mode), .flags = kParamClamp },
            { .id = 2, .name = "octaves", .minValue = 1.0f, .maxValue = static_cast<float>(kMaxOctaves), .defaultValue = 1.0f, .kind = ParamKind::IntRound, .offset = offsetof(ArpeggiatorParams, octaves), .flags = kParamClamp },
            { .id = 3, .name = "gate", .minValue = 0.05f, .maxValue = 1.0f, .defaultValue = 0.6f, .offset = offsetof(ArpeggiatorParams, gate), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Note, "Notes" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Note, "Arp" } };

         const std::vector<SliderSpec>& arpSpecs()
         {
            static const std::vector<SliderSpec> specs =
            slidersFromParams(kParams,
                              {
                              { "rate", "Rate", SliderFormat::Choice, { "1/4", "1/8", "1/16", "1/32" } },
                              { "mode", "Mode", SliderFormat::Choice, { "Up", "Down", "Up/Down", "Random", "As played" } },
                              { "octaves", "Octaves", SliderFormat::Integer },
                              { "gate", "Gate", SliderFormat::Percent },
                              });
            return specs;
         }

         struct PatternNote
         {
            int pitch = 60;
            int channel = 0;
            float velocity = 0.0f;
         };

         /** The held notes expanded over `octaves`, in the order the mode walks them. */
         int buildPattern(const ArpeggiatorRuntimeState& state, int octaves, bool sorted, PatternNote* out)
         {
            ArpeggiatorRuntimeState::Held base[kArpMaxHeld];
            const int n = state.heldCount;
            std::copy(state.held, state.held + n, base);
            if (sorted)
            {
               std::sort(base, base + n,
                         [](const ArpeggiatorRuntimeState::Held& a, const ArpeggiatorRuntimeState::Held& b)
                         {
                            return a.pitch < b.pitch;
                         });
            }
            int count = 0;
            for (int octave = 0; octave < octaves; ++octave)
            {
               for (int i = 0; i < n; ++i)
               {
                  const int pitch = base[i].pitch + 12 * octave;
                  if (pitch > 127)
                     continue;
                  out[count++] = { pitch, base[i].channel, base[i].velocity };
               }
            }
            return count;
         }

         uint32_t nextRandom(uint32_t& rng)
         {
            rng = rng * 1664525u + 1013904223u;
            return rng >> 8;
         }
      }

      double ArpeggiatorModuleAdapter::stepBeats(int rate)
      {
         switch (rate)
         {
            case 0:
               return 1.0;
            case 1:
               return 0.5;
            case 3:
               return 0.125;
            default:
               return 0.25;
         }
      }

      ParamTable ArpeggiatorModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> ArpeggiatorModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> ArpeggiatorModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> ArpeggiatorModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), arpSpecs());
      }

      void ArpeggiatorModuleAdapter::processNotes(void* runtimeState,
                                                  const void* paramsPtr,
                                                  const WasmNoteProcessBlock& block,
                                                  WasmNoteEvent* outNotes,
                                                  int maxNotes,
                                                  int& outCount) const
      {
         NoteWriter writer(outNotes, maxNotes, outCount);
         if (!runtimeState || !paramsPtr || !writer.valid())
            return;

         auto& state = *static_cast<ArpeggiatorRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const ArpeggiatorParams*>(paramsPtr);
         const double step = stepBeats(params.rate);
         const int octaves = std::max(1, std::min(kMaxOctaves, params.octaves));
         const double gateBeats = std::max(0.05, std::min(1.0, static_cast<double>(params.gate))) * step;

         auto stopSounding = [&]()
         {
            if (!state.sounding)
               return;
            writer.push(state.soundingPitch, 0.0f, false, state.soundingChannel);
            state.sounding = false;
            state.noteOffBeat = -1.0;
         };

         // Held notes follow the input; the pattern walks whatever is held when a step fires.
         for (int n = 0; n < block.noteCount; ++n)
         {
            const WasmNoteEvent& in = block.notes[n];
            // A bend belongs to one held key; the arp replays keys as new notes, so it does not follow.
            if (in.pitch < 0 || in.pitch > 127 || isPitchBendEvent(in))
               continue;
            int found = -1;
            for (int i = 0; i < state.heldCount; ++i)
            {
               if (state.held[i].pitch == in.pitch)
               {
                  found = i;
                  break;
               }
            }
            if (in.isNoteOn)
            {
               if (found >= 0)
               {
                  state.held[found].velocity = in.velocity;
                  state.held[found].channel = static_cast<uint8_t>(in.channel & 0x0f);
               }
               else if (state.heldCount < kArpMaxHeld)
               {
                  state.held[state.heldCount++] = { static_cast<uint8_t>(in.pitch),
                                                    static_cast<uint8_t>(in.channel & 0x0f), in.velocity };
               }
            }
            else if (found >= 0)
            {
               for (int i = found; i + 1 < state.heldCount; ++i)
                  state.held[i] = state.held[i + 1];
               --state.heldCount;
            }
         }
         // Nothing left to play: do not let the last arp note ring until its gate runs out.
         if (state.heldCount == 0)
            stopSounding();

         auto releaseDue = [&](double limitBeat)
         {
            if (state.sounding && state.noteOffBeat >= 0.0 && state.noteOffBeat <= limitBeat + kBeatEpsilon)
               stopSounding();
         };

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
            // A step that arrives before the gate closed still ends the previous note.
            stopSounding();
            if (state.heldCount == 0)
               continue;

            PatternNote pattern[kArpMaxHeld * kMaxOctaves];
            const bool sorted = params.mode != static_cast<int>(ArpMode::AsPlayed);
            const int count = buildPattern(state, octaves, sorted, pattern);
            if (count == 0)
               continue;

            int index = 0;
            switch (static_cast<ArpMode>(params.mode))
            {
               case ArpMode::Down:
                  index = count - 1 - (state.position % count);
                  break;
               case ArpMode::UpDown:
               {
                  const int period = std::max(1, 2 * count - 2);
                  const int phase = state.position % period;
                  index = phase < count ? phase : period - phase;
                  break;
               }
               case ArpMode::Random:
                  index = static_cast<int>(nextRandom(state.rng) % static_cast<uint32_t>(count));
                  break;
               default:
                  index = state.position % count;
                  break;
            }
            // Bounded counter: wraps after ~1M steps (hours at the fastest rate), not an audible seam.
            state.position = (state.position + 1) & 0xFFFFF;

            const PatternNote& note = pattern[index];
            writer.push(note.pitch, note.velocity, true, note.channel);
            state.sounding = true;
            state.soundingPitch = note.pitch;
            state.soundingChannel = note.channel;
            state.noteOffBeat = stepBeat + gateBeats;
         }

         releaseDue(block.beatEnd);
      }

      BESPOKE_REGISTER_MODULE(ArpeggiatorModuleAdapter);

   } // namespace wasm
} // namespace bespoke
