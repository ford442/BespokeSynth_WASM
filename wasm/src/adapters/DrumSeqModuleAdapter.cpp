/**
 * BespokeSynth WASM - Four-track drum step sequencer adapter + canvas UI
 */

#include "BespokeWasm/adapters/DrumSeqModuleAdapter.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr double kStepsPerBeat = 4.0;
         constexpr float kPortStep = 15.0f;
         constexpr float kCell = 9.0f;
         constexpr float kGap = 2.0f;
         constexpr float kLabelW = 28.0f;
         constexpr float kPad = 8.0f;

         const char* const kTrackNames[kDrumSeqTracks] = { "kick", "snare", "hat", "tom" };
         const char* const kTrackPitchNames[kDrumSeqTracks] = { "kickpitch", "snarepitch", "hatpitch", "tompitch" };
         const char* const kTrackLabels[kDrumSeqTracks] = { "Kick", "Snr", "Hat", "Tom" };
         constexpr int kDefaultMask[kDrumSeqTracks] = { 0x1111, 0x1010, 0x4444, 0x0000 };
         constexpr int kDefaultPitch[kDrumSeqTracks] = { 36, 38, 42, 45 };

         int clampInt(int value, int lo, int hi)
         {
            return std::max(lo, std::min(hi, value));
         }

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "kick", .minValue = 0.0f, .maxValue = 65535.0f, .defaultValue = static_cast<float>(kDefaultMask[0]), .kind = ParamKind::Mask16, .offset = offsetof(DrumSeqParams, patternMask[0]) },
            { .id = 1, .name = "snare", .minValue = 0.0f, .maxValue = 65535.0f, .defaultValue = static_cast<float>(kDefaultMask[1]), .kind = ParamKind::Mask16, .offset = offsetof(DrumSeqParams, patternMask[1]) },
            { .id = 2, .name = "hat", .minValue = 0.0f, .maxValue = 65535.0f, .defaultValue = static_cast<float>(kDefaultMask[2]), .kind = ParamKind::Mask16, .offset = offsetof(DrumSeqParams, patternMask[2]) },
            { .id = 3, .name = "tom", .minValue = 0.0f, .maxValue = 65535.0f, .defaultValue = static_cast<float>(kDefaultMask[3]), .kind = ParamKind::Mask16, .offset = offsetof(DrumSeqParams, patternMask[3]) },
            { .id = 4, .name = "kickpitch", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = static_cast<float>(kDefaultPitch[0]), .kind = ParamKind::IntRound, .offset = offsetof(DrumSeqParams, pitch[0]), .flags = kParamClamp },
            { .id = 5, .name = "snarepitch", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = static_cast<float>(kDefaultPitch[1]), .kind = ParamKind::IntRound, .offset = offsetof(DrumSeqParams, pitch[1]), .flags = kParamClamp },
            { .id = 6, .name = "hatpitch", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = static_cast<float>(kDefaultPitch[2]), .kind = ParamKind::IntRound, .offset = offsetof(DrumSeqParams, pitch[2]), .flags = kParamClamp },
            { .id = 7, .name = "tompitch", .minValue = 0.0f, .maxValue = 127.0f, .defaultValue = static_cast<float>(kDefaultPitch[3]), .kind = ParamKind::IntRound, .offset = offsetof(DrumSeqParams, pitch[3]), .flags = kParamClamp },
            { .id = 8, .name = "steps", .minValue = 1.0f, .maxValue = static_cast<float>(kDrumSeqMaxSteps), .defaultValue = static_cast<float>(kDrumSeqMaxSteps), .kind = ParamKind::IntRound, .offset = offsetof(DrumSeqParams, steps), .flags = kParamClamp },
            { .id = 9, .name = "gate", .minValue = 0.05f, .maxValue = 1.0f, .defaultValue = 0.5f, .offset = offsetof(DrumSeqParams, gate), .flags = kParamClamp },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Pulse, "Clock" } };
         constexpr PortDescriptor kOutputs[] = {
            { PortType::Note, "Kick" },
            { PortType::Note, "Snare" },
            { PortType::Note, "Hat" },
            { PortType::Note, "Tom" },
         };
      }

      DrumSeqModule::DrumSeqModule(int id)
      : Module(id, "drumseq", "Drum Sequencer", ModuleCategory::Pulse)
      {
         const float gridW = kLabelW + kDrumSeqMaxSteps * (kCell + kGap);
         setSize(kPad * 2.0f + gridW, gridTop() + kDrumSeqTracks * (kCell + kGap) + kPad);
      }

      float DrumSeqModule::gridTop() const
      {
         const size_t portRows = std::max(mInputs.size(), mOutputs.size());
         return kTitleBarHeight + 6.0f + static_cast<float>(portRows) * kPortStep;
      }

      bool DrumSeqModule::isStepActive(int track, int step) const
      {
         if (track < 0 || track >= kDrumSeqTracks || step < 0 || step >= kDrumSeqMaxSteps)
            return false;
         return (mPatternMask[track] & (1 << step)) != 0;
      }

      void DrumSeqModule::setStepActive(int track, int step, bool active)
      {
         if (track < 0 || track >= kDrumSeqTracks || step < 0 || step >= kDrumSeqMaxSteps)
            return;
         if (active)
            mPatternMask[track] |= (1 << step);
         else
            mPatternMask[track] &= ~(1 << step);
      }

      void DrumSeqModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         const float screenX = (mX + offsetX) * scale;
         const float screenY = (mY + offsetY) * scale;
         const float font = UITheme::kValueFontSize * scale;
         const float cell = kCell * scale;
         const float gap = kGap * scale;
         const float gridX = screenX + (kPad + kLabelW) * scale;
         const float top = screenY + gridTop() * scale;

         for (int track = 0; track < kDrumSeqTracks; ++track)
         {
            const float rowY = top + static_cast<float>(track) * (cell + gap);
            drawText(renderer, screenX + kPad * scale, textBaselineCentered(rowY + cell * 0.5f, font),
                     kTrackLabels[track], UITheme::kTextSecondary, font);
            for (int step = 0; step < kDrumSeqMaxSteps; ++step)
            {
               const float cx = gridX + static_cast<float>(step) * (cell + gap);
               if (step >= mSteps)
                  renderer.fillColor(Color(0.12f, 0.12f, 0.13f, 1.0f));
               else if (isStepActive(track, step))
                  renderer.fillColor(Color(0.95f, 0.45f, 0.35f, 1.0f));
               else
                  renderer.fillColor((step % 4) == 0 ? Color(0.28f, 0.28f, 0.32f, 1.0f)
                                                     : Color(0.18f, 0.18f, 0.20f, 1.0f));
               renderer.roundedRect(cx, rowY, cell, cell, 2.0f * scale);
               renderer.fill();
            }
         }
      }

      bool DrumSeqModule::handleMouseDown(float worldX, float worldY)
      {
         const float localX = worldX - mX - kPad - kLabelW;
         const float localY = worldY - mY - gridTop();
         if (localX < 0.0f || localY < 0.0f)
            return false;
         const int step = static_cast<int>(localX / (kCell + kGap));
         const int track = static_cast<int>(localY / (kCell + kGap));
         if (step >= mSteps || track >= kDrumSeqTracks)
            return false;
         setStepActive(track, step, !isStepActive(track, step));
         return true;
      }

      std::string DrumSeqModule::controlNameAt(float worldX, float worldY) const
      {
         // A track's label names its pitch; its cells name its pattern.
         const float localX = worldX - mX - kPad;
         const float localY = worldY - mY - gridTop();
         if (localX < 0.0f || localY < 0.0f || worldX > mX + mWidth)
            return {};
         const int track = static_cast<int>(localY / (kCell + kGap));
         if (track >= kDrumSeqTracks)
            return {};
         return localX < kLabelW ? kTrackPitchNames[track] : kTrackNames[track];
      }

      void DrumSeqModule::setControlValue(const std::string& name, float value)
      {
         for (int track = 0; track < kDrumSeqTracks; ++track)
         {
            if (name == kTrackNames[track])
            {
               mPatternMask[track] = static_cast<int>(value) & 0xFFFF;
               return;
            }
            if (name == kTrackPitchNames[track])
            {
               mPitch[track] = clampInt(static_cast<int>(std::lround(value)), 0, 127);
               return;
            }
         }
         if (name == "steps")
            mSteps = clampInt(static_cast<int>(std::lround(value)), 1, kDrumSeqMaxSteps);
         else if (name == "gate")
            mGate = std::max(0.05f, std::min(1.0f, value));
      }

      float DrumSeqModule::getControlValue(const std::string& name) const
      {
         for (int track = 0; track < kDrumSeqTracks; ++track)
         {
            if (name == kTrackNames[track])
               return static_cast<float>(mPatternMask[track]);
            if (name == kTrackPitchNames[track])
               return static_cast<float>(mPitch[track]);
         }
         if (name == "steps")
            return static_cast<float>(mSteps);
         if (name == "gate")
            return mGate;
         return 0.0f;
      }

      ParamTable DrumSeqModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> DrumSeqModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> DrumSeqModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> DrumSeqModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<DrumSeqModule>(id);
      }

      void DrumSeqModuleAdapter::emitNotesForBeatRange(void* runtimeState,
                                                       const void* paramsPtr,
                                                       double beatStart,
                                                       double beatEnd,
                                                       WasmNoteEvent* outNotes,
                                                       int maxNotes,
                                                       int& outCount,
                                                       const WasmPulseEvent* pulses,
                                                       int pulseCount) const
      {
         outCount = 0;
         if (!outNotes || maxNotes <= 0 || !runtimeState || !paramsPtr)
            return;

         auto& state = *static_cast<DrumSeqRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const DrumSeqParams*>(paramsPtr);
         const int steps = clampInt(params.steps, 1, kDrumSeqMaxSteps);
         const double gateBeats = static_cast<double>(params.gate) / kStepsPerBeat;

         auto push = [&](int track, int pitch, float velocity, bool on)
         {
            if (outCount >= maxNotes)
               return;
            WasmNoteEvent event;
            event.pitch = pitch;
            event.velocity = velocity;
            event.isNoteOn = on;
            event.outputPort = track;
            outNotes[outCount++] = event;
         };

         auto releaseDue = [&](double limitBeat)
         {
            for (int track = 0; track < kDrumSeqTracks; ++track)
            {
               if (state.noteIsOn[track] && state.noteOffBeat[track] > beatStart &&
                   state.noteOffBeat[track] <= limitBeat)
               {
                  push(track, state.activePitch[track], 0.0f, false);
                  state.noteIsOn[track] = false;
                  state.noteOffBeat[track] = -1.0;
               }
            }
         };

         for (int i = 0; i < pulseCount && pulses; ++i)
         {
            const double pulseBeat = pulses[i].timeBeats;
            if (pulseBeat < beatStart || pulseBeat >= beatEnd)
               continue;
            releaseDue(pulseBeat);

            const int step = static_cast<int>(state.stepCounter++ % steps);
            for (int track = 0; track < kDrumSeqTracks; ++track)
            {
               if ((params.patternMask[track] & (1 << step)) == 0)
                  continue;
               if (state.noteIsOn[track])
                  push(track, state.activePitch[track], 0.0f, false);
               push(track, params.pitch[track], pulses[i].strength, true);
               state.noteIsOn[track] = true;
               state.activePitch[track] = params.pitch[track];
               state.noteOffBeat[track] = pulseBeat + gateBeats;
            }
         }

         releaseDue(beatEnd);
      }

      BESPOKE_REGISTER_MODULE(DrumSeqModuleAdapter);

   } // namespace wasm
} // namespace bespoke
