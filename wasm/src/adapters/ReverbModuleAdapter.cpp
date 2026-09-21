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

      std::vector<WasmControlDescriptor> ReverbModuleAdapter::controlDescriptors() const
      {
         return {
            { "room", 0.7f },
            { "damping", 0.3f },
            { "mix", 0.35f },
         };
      }

      std::vector<PortDescriptor> ReverbModuleAdapter::inputPorts() const
      {
         return { { PortType::Audio, "In" } };
      }

      std::vector<PortDescriptor> ReverbModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> ReverbModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<ReverbModule>(id);
      }

      void ReverbModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) ReverbParams();
         params->roomSize = wasmControlValue(controls, "room", 0.7f);
         params->damping = wasmControlValue(controls, "damping", 0.3f);
         params->mix = wasmControlValue(controls, "mix", 0.35f);
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
                                             float* buffer,
                                             const WasmAudioProcessContext& context) const
      {
         if (!runtimeState || !paramsPtr || !buffer)
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

         // Mono graph: the same signal feeds both inputs (freeverb sums L+R, so the input is
         // doubled) and the decorrelated outputs are averaged. The 0.5 undoes the doubling;
         // with scalewet that puts 100% wet at roughly 0.7x the input RMS on white noise.
         const float wetGain = mix * scalewet * 0.5f;
         const float dryGain = 1.0f - mix;
         float in[kChunk];
         float outL[kChunk];
         float outR[kChunk];
         for (int start = 0; start < context.numSamples; start += kChunk)
         {
            const int count = std::min(kChunk, context.numSamples - start);
            std::memcpy(in, buffer + start, static_cast<size_t>(count) * sizeof(float));
            model.processreplace(in, in, outL, outR, count, 1);
            for (int i = 0; i < count; ++i)
               buffer[start + i] = in[i] * dryGain + (outL[i] + outR[i]) * 0.5f * wetGain;
         }
      }

      BESPOKE_REGISTER_MODULE(ReverbModuleAdapter);

   } // namespace wasm
} // namespace bespoke
