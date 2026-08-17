/**
 * BespokeSynth WASM - Schroeder reverb adapter + canvas UI
 *
 * Demonstrates the self-contained adapter pattern: one header, one source.
 */

#include "BespokeWasm/adapters/ReverbModuleAdapter.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

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

         // Comb lengths at 44.1 kHz (Freeverb-style tunings, first four).
         constexpr int kCombTunings441[] = { 1116, 1188, 1277, 1356 };
         constexpr int kAllpassTunings441[] = { 556, 441 };
      }

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

      void ReverbModuleAdapter::initRuntimeState(void* runtimeState) const
      {
         new (runtimeState) ReverbAdapterRuntimeState();
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

         auto& state = *static_cast<ReverbAdapterRuntimeState*>(runtimeState);
         const auto& params = *static_cast<const ReverbParams*>(paramsPtr);
         const float sampleRate = std::max(1.0f, context.sampleRate);
         const float rateScale = sampleRate / 44100.0f;

         if (state.lastSampleRate != sampleRate)
         {
            std::memset(&state, 0, sizeof(state));
            for (int i = 0; i < ReverbAdapterRuntimeState::kCombCount; ++i)
            {
               const int size = std::max(16, std::min(ReverbAdapterRuntimeState::kMaxComb,
                                                      static_cast<int>(kCombTunings441[i] * rateScale)));
               state.combSize[i] = size;
            }
            for (int i = 0; i < ReverbAdapterRuntimeState::kAllpassCount; ++i)
            {
               const int size = std::max(16, std::min(ReverbAdapterRuntimeState::kMaxAllpass,
                                                      static_cast<int>(kAllpassTunings441[i] * rateScale)));
               state.allpassSize[i] = size;
            }
            state.lastSampleRate = sampleRate;
         }

         const float room = clampFloat(params.roomSize, 0.0f, 1.0f);
         const float damp = clampFloat(params.damping, 0.0f, 1.0f);
         const float mix = clampFloat(params.mix, 0.0f, 1.0f);
         const float feedback = 0.28f + room * 0.56f;

         for (int n = 0; n < context.numSamples; ++n)
         {
            const float input = buffer[n];
            float acc = 0.0f;
            for (int c = 0; c < ReverbAdapterRuntimeState::kCombCount; ++c)
            {
               const int size = std::max(1, state.combSize[c]);
               int& index = state.combIndex[c];
               float output = state.comb[c][index];
               state.combFilter[c] = output * (1.0f - damp) + state.combFilter[c] * damp;
               state.comb[c][index] = input + state.combFilter[c] * feedback;
               index = (index + 1) % size;
               acc += output;
            }
            acc *= 0.25f;

            for (int a = 0; a < ReverbAdapterRuntimeState::kAllpassCount; ++a)
            {
               const int size = std::max(1, state.allpassSize[a]);
               int& index = state.allpassIndex[a];
               const float bufOut = state.allpass[a][index];
               const float ret = acc + bufOut * 0.5f;
               state.allpass[a][index] = acc + bufOut * -0.5f;
               index = (index + 1) % size;
               acc = ret;
            }

            buffer[n] = input * (1.0f - mix) + acc * mix;
         }
      }

      BESPOKE_REGISTER_MODULE(ReverbModuleAdapter);

   } // namespace wasm
} // namespace bespoke
