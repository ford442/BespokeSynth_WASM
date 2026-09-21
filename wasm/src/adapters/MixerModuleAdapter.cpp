/**
 * BespokeSynth WASM - Four-channel summing mixer adapter implementation
 */

#include "BespokeWasm/adapters/MixerModuleAdapter.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/SliderPanelModule.h"
#include <new>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         const char* const kLevelNames[kMixerChannelCount] = { "level1", "level2", "level3", "level4" };

         const std::vector<SliderSpec>& mixerSpecs()
         {
            static const std::vector<SliderSpec> specs = {
               { "level1", "Ch 1", 0.0f, 1.0f, 0.8f, SliderFormat::Percent },
               { "level2", "Ch 2", 0.0f, 1.0f, 0.8f, SliderFormat::Percent },
               { "level3", "Ch 3", 0.0f, 1.0f, 0.8f, SliderFormat::Percent },
               { "level4", "Ch 4", 0.0f, 1.0f, 0.8f, SliderFormat::Percent },
               { "master", "Master", 0.0f, 1.0f, 1.0f, SliderFormat::Percent },
            };
            return specs;
         }
      }

      std::vector<WasmControlDescriptor> MixerModuleAdapter::controlDescriptors() const
      {
         return controlDescriptorsFromSpecs(mixerSpecs());
      }

      std::vector<PortDescriptor> MixerModuleAdapter::inputPorts() const
      {
         return {
            { PortType::Audio, "In 1" },
            { PortType::Audio, "In 2" },
            { PortType::Audio, "In 3" },
            { PortType::Audio, "In 4" },
         };
      }

      std::vector<PortDescriptor> MixerModuleAdapter::outputPorts() const
      {
         return { { PortType::Audio, "Out" } };
      }

      std::unique_ptr<Module> MixerModuleAdapter::createUiModule(int id) const
      {
         return std::make_unique<SliderPanelModule>(id, typeId(), displayName(), category(), mixerSpecs(), 150.0f);
      }

      void MixerModuleAdapter::fillParams(const WasmControlMap& controls, void* dst) const
      {
         auto* params = new (dst) MixerParams();
         for (int ch = 0; ch < kMixerChannelCount; ++ch)
            params->level[ch] = sliderSpecValue(mixerSpecs(), controls, kLevelNames[ch]);
         params->master = sliderSpecValue(mixerSpecs(), controls, "master");
      }

      void MixerModuleAdapter::processAudio(void* runtimeState,
                                            const void* paramsPtr,
                                            float* buffer,
                                            const WasmAudioProcessContext& context) const
      {
         (void)runtimeState;
         if (!paramsPtr || !buffer)
            return;

         // `buffer` arrives zeroed (sumsAudioInputs() == false); a port may carry several cables.
         const auto& params = *static_cast<const MixerParams*>(paramsPtr);
         for (int c = 0; c < context.audioInputCount; ++c)
         {
            const WasmAudioInput& input = context.audioInputs[c];
            if (!input.buffer || input.port < 0 || input.port >= kMixerChannelCount)
               continue;
            const float gain = params.level[input.port] * params.master;
            if (gain > 0.0f)
               dsp::accumulateScaled(buffer, input.buffer, context.numSamples, gain);
         }
      }

      BESPOKE_REGISTER_MODULE(MixerModuleAdapter);

   } // namespace wasm
} // namespace bespoke
