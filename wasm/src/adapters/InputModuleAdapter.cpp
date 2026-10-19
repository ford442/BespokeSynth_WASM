/**
 * BespokeSynth WASM - Input (microphone) source adapter implementation
 */

#include "BespokeWasm/adapters/InputModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/InputAudioBus.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr float kStatusHeight = 24.0f;
         constexpr float kMeterHeight = 6.0f;

         const std::vector<SliderSpec>& inputSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "gain", "Gain", 0.0f, 2.0f, 1.0f, SliderFormat::Percent },
               { "mute", "Mic", 0.0f, 1.0f, 0.0f, SliderFormat::Choice, { "Live", "Muted" } },
            };
            return specs;
         }
      }

      InputModule::InputModule(int id, std::vector<SliderSpec> specs)
      : SliderPanelModule(id, "input", "Input", ModuleCategory::Synth, std::move(specs), 150.0f)
      {
         setSize(mWidth, mHeight + kStatusHeight);
      }

      void InputModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         SliderPanelModule::render(renderer, offsetX, offsetY, scale);

         auto& bus = InputAudioBus::instance();
         // Fast attack, slow release, so a short syllable is still visible for a few frames.
         mLevel = std::max(bus.takePeak(), mLevel * 0.9f);

         const float screenX = (mX + offsetX) * scale;
         const float screenY = (mY + offsetY) * scale;
         const float font = UITheme::kValueFontSize * scale;
         const float top = screenY + (mHeight - kStatusHeight) * scale;
         const float barX = screenX + kSidePad * scale;
         const float barW = (mWidth - 2.0f * kSidePad) * scale;

         if (!bus.captureActive())
         {
            drawText(renderer, barX, textBaselineFromTop(renderer, top + 4.0f * scale),
                     "Mic off - press Input", UITheme::kTextSecondary, font);
            return;
         }
         const float level = std::max(0.0f, std::min(1.0f, mLevel));
         renderer.drawSlider(barX, top + 8.0f * scale, barW, kMeterHeight * scale, level,
                             Color(0.2f, 0.2f, 0.22f, 1.0f),
                             level > 0.9f ? Color(0.9f, 0.3f, 0.25f, 0.9f) : Color(0.35f, 0.75f, 0.45f, 0.9f));
      }

      std::vector<WasmControlDescriptor> InputModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(inputSpecs());
      }

      std::vector<PortDescriptor> InputModuleAdapter::inputPorts() const
      {
         return {};
      }

      std::vector<PortDescriptor> InputModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> InputModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<InputModule>(id, inputSpecs());
      }

      void InputModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         const auto& specs = inputSpecs();
         auto* params = new (dst) InputParams();
         params->gain = sliderSpecValue(specs, controls, "gain");
         params->mute = std::lround(sliderSpecValue(specs, controls, "mute")) == 1;
      }

      void InputModuleAdapter::processAudio(void* runtimeState,
                                            const void* paramsPtr,
                                            float* const* channels,
                                            const WasmAudioProcessContext& context) const
      {
         (void)runtimeState;
         if (!paramsPtr || !channels || !channels[0] || !context.inputAudio)
            return;
         const auto& params = *static_cast<const InputParams*>(paramsPtr);
         if (params.mute || params.gain <= 0.0f)
            return; // the engine hands sources a zeroed block
         for (int ch = 0; ch < context.numChannels; ++ch)
         {
            if (channels[ch])
               dsp::accumulateScaled(channels[ch], context.inputAudio, context.numSamples, params.gain);
         }
      }

      BESPOKE_REGISTER_MODULE(InputModuleAdapter);

   } // namespace wasm
} // namespace bespoke
