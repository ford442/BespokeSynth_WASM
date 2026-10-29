/**
 * BespokeSynth WASM - Freeverb reverb adapter + canvas UI
 *
 * DSP is libs/freeverb (public domain, Jezar at Dreampoint), the same kernel
 * the desktop FreeverbEffect wraps. The desktop effect class itself is not
 * compiled (IDrawableModule closure).
 */

#include "BespokeWasm/adapters/ReverbModuleAdapter.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

#include "revmodel.hpp"

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }

         constexpr int kChunk = 64;

         constexpr ParamDescriptor kParams[] = {
            { .id = 0, .name = "room", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.7f, .offset = offsetof(ReverbParams, roomSize) },
            { .id = 1, .name = "damping", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.3f, .offset = offsetof(ReverbParams, damping) },
            { .id = 2, .name = "mix", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.35f, .offset = offsetof(ReverbParams, mix), .smoothingMs = kParamSmoothingMs },
         };
         static_assert(paramTableIdsAreIndices(kParams));

         constexpr PortDescriptor kInputs[] = { { PortType::Audio, "In" } };
         constexpr PortDescriptor kOutputs[] = { { PortType::Audio, "Out" } };
      }

      /**
       * revmodel points its combs/allpasses at its own member buffers, so it must
       * not be byte-moved. The engine's runtime arena can reallocate when a module
       * is added; `boundTo` detects that and the model is rebuilt in place (the
       * tail restarts, nothing dangles).
       */
      struct ReverbAdapterRuntimeState
      {
         revmodel model;
         const void* boundTo = nullptr;
      };

      ReverbModule::ReverbModule(int id)
      : Module(id, "reverb", "Reverb", ModuleCategory::AudioEffect)
      {
         setSize(140, 100);
      }

      void ReverbModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         float screenX = (mX + offsetX) * scale;
         float screenY = (mY + offsetY) * scale;
         const float contentTop = screenY + (kTitleBarHeight + 18) * scale;
         const float valueFont = UITheme::kValueFontSize * scale;
         const float lineH = textLineSpacing(valueFont, scale);
         const float textX = screenX + 10 * scale;

         char roomText[32];
         snprintf(roomText, sizeof(roomText), "Room %.0f%%", mRoomSize * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 0, valueFont, scale),
                  roomText, UITheme::kTextValue, valueFont);

         char dampText[32];
         snprintf(dampText, sizeof(dampText), "Damp %.0f%%", mDamping * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 1, valueFont, scale),
                  dampText, UITheme::kTextSecondary, valueFont);

         char mixText[32];
         snprintf(mixText, sizeof(mixText), "Mix %.0f%%", mMix * 100.0f);
         drawText(renderer, textX, textBaselineForLine(contentTop, 2, valueFont, scale),
                  mixText, UITheme::kTextSecondary, valueFont);

         (void)lineH;
      }

      std::string ReverbModule::controlNameAt(float worldX, float worldY) const
      {
         return textLineControlAt(worldX, worldY, kTitleBarHeight + 18.0f, UITheme::kValueFontSize,
                                  { "room", "damping", "mix" });
      }

      void ReverbModule::setControlValue(const std::string& name, float value)
      {
         if (name == "room")
            mRoomSize = value;
         else if (name == "damping")
            mDamping = value;
         else if (name == "mix")
            mMix = value;
      }

      float ReverbModule::getControlValue(const std::string& name) const
      {
         if (name == "room")
            return mRoomSize;
         if (name == "damping")
            return mDamping;
         if (name == "mix")
            return mMix;
         return 0.0f;
      }

      ParamTable ReverbModuleAdapter::paramTable() const
      {
         return kParams;
      }

      std::span<const PortDescriptor> ReverbModuleAdapter::inputPorts() const
      {
         return kInputs;
      }

      std::span<const PortDescriptor> ReverbModuleAdapter::outputPorts() const
      {
         return kOutputs;
      }

      std::unique_ptr<Module> ReverbModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<ReverbModule>(id);
      }

      void ReverbModuleAdapter::initParams(void* params) const
      {
         new (params) ReverbParams();
      }

      size_t ReverbModuleAdapter::runtimeStateSize() const
      {
         return sizeof(ReverbAdapterRuntimeState);
      }

      void ReverbModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         auto* state = new (runtimeState) ReverbAdapterRuntimeState();
         state->boundTo = state;
      }

      void ReverbModuleAdapter::destroyRuntimeState(void* runtimeState) const
      {
         static_cast<ReverbAdapterRuntimeState*>(runtimeState)->~ReverbAdapterRuntimeState();
      }

      void ReverbModuleAdapter::processAudio(void* runtimeState,
                                             const void* paramsPtr,
                                             float* const* channels,
                                             const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !channels)
            return;

         auto* state = static_cast<ReverbAdapterRuntimeState*>(runtimeState);
         if (state->boundTo != state)
         {
            // Relocated by an arena grow: the old object's pointers are stale, so
            // construct over it without running its destructor (it owns nothing).
            state = new (runtimeState) ReverbAdapterRuntimeState();
            state->boundTo = state;
         }
         const auto& params = *static_cast<const ReverbParams*>(paramsPtr);
         const float room = clampFloat(params.roomSize, 0.0f, 1.0f);
         const float damp = clampFloat(params.damping, 0.0f, 1.0f);
         const float mix = clampFloat(params.mix, 0.0f, 1.0f);

         // Freeverb's own mapping: room → comb feedback 0.7..0.98, damp → 0..0.4 (setdamp takes %).
         revmodel& model = state->model;
         model.setroomsize(offsetroom + room * scaleroom);
         model.setdamp(damp * 100.0f);
         model.setwidth(100.0f);
         model.setwet(1.0f);
         model.setdry(0.0f);
         model.update();

         // Freeverb sums L+R into its tank, so a mono source (upmixed to identical L/R by the
         // engine) is doubled; the 0.5 undoes that. With scalewet, 100% wet sits at roughly
         // 0.7x the input RMS per channel on white noise. The L/R tails stay decorrelated.
         const float wetGain = mix * scalewet * 0.5f;
         const float dryGain = 1.0f - mix;
         float inL[kChunk];
         float inR[kChunk];
         float outL[kChunk];
         float outR[kChunk];
         for (int start = 0; start < context.numSamples; start += kChunk)
         {
            const int count = std::min(kChunk, context.numSamples - start);
            std::memcpy(inL, channels[0] + start, static_cast<size_t>(count) * sizeof(float));
            if (context.numChannels < 2)
            {
               // One-channel block (processMono): the two tails are averaged back to mono.
               model.processreplace(inL, inL, outL, outR, count, 1);
               for (int i = 0; i < count; ++i)
                  channels[0][start + i] = inL[i] * dryGain + (outL[i] + outR[i]) * 0.5f * wetGain;
               continue;
            }
            std::memcpy(inR, channels[1] + start, static_cast<size_t>(count) * sizeof(float));
            model.processreplace(inL, inR, outL, outR, count, 1);
            for (int i = 0; i < count; ++i)
            {
               channels[0][start + i] = inL[i] * dryGain + outL[i] * wetGain;
               channels[1][start + i] = inR[i] * dryGain + outR[i] * wetGain;
            }
         }
      }

      BESPOKE_REGISTER_MODULE(ReverbModuleAdapter);

   } // namespace wasm
} // namespace bespoke
