/**
 * BespokeSynth WASM - Slider panel with a sample
 */

#include "BespokeWasm/SamplePanelModule.h"
#include "BespokeWasm/BundledKit.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/SampleSource.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         // Room for the waveform and the sample-name line under it.
         constexpr float kHeaderHeight = SamplePanelModule::kWaveHeight + 18.0f;
      }

      SamplePanelModule::SamplePanelModule(int id,
                                           const std::string& type,
                                           const std::string& name,
                                           ModuleCategory category,
                                           std::vector<SliderSpec> specs,
                                           float width,
                                           const char* markerControl,
                                           std::string defaultSampleHash)
      : SliderPanelModule(id, type, name, category, std::move(specs), width, kHeaderHeight)
      , mSampleHash(std::move(defaultSampleHash))
      , mMarkerControl(markerControl)
      {
         if (!mSampleHash.empty())
            refreshSample();
      }

      void SamplePanelModule::refreshSample()
      {
         const SampleBuffer* sample = mSampleHash.empty() ? nullptr : findSampleOrBundled(mSampleHash);
         mSampleHandle = sample ? SampleStore::instance().handleFor(sample->id()) : SampleHandle{};
         mSampleName = sample ? sample->name() : std::string();
      }

      void SamplePanelModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         SliderPanelModule::render(renderer, offsetX, offsetY, scale);

         const float waveX = (mX + offsetX + kSidePad) * scale;
         const float waveY = (mY + offsetY + slidersTop() - kHeaderHeight) * scale;
         const float waveW = (mWidth - 2.0f * kSidePad) * scale;
         const float waveH = kWaveHeight * scale;
         renderer.fillColor(Color(0.08f, 0.08f, 0.1f, 1.0f));
         renderer.rect(waveX, waveY, waveW, waveH);
         renderer.fill();

         auto& store = SampleStore::instance();
         const SampleBuffer* sample = store.resolve(mSampleHandle);
         if (!sample && !mSampleHash.empty())
         {
            // Reloaded under a new generation (undo after eviction); never decodes from here.
            sample = store.findByHash(mSampleHash);
            if (sample)
               mSampleHandle = store.handleFor(sample->id());
         }
         if (sample && sample->peaks().size() > 1)
         {
            const auto& peaks = sample->peaks();
            renderer.strokeColor(Color(0.45f, 0.82f, 0.7f, 1.0f));
            renderer.strokeWidth(1.0f);
            renderer.beginPath();
            for (size_t i = 0; i < peaks.size(); ++i)
            {
               const float x = waveX + (static_cast<float>(i) / static_cast<float>(peaks.size() - 1)) * waveW;
               const float y = waveY + (1.0f - (peaks[i].max * 0.5f + 0.5f)) * waveH;
               if (i == 0)
                  renderer.moveTo(x, y);
               else
                  renderer.lineTo(x, y);
            }
            renderer.stroke();
         }

         if (mMarkerControl)
         {
            const float marker = std::max(0.0f, std::min(1.0f, getControlValue(mMarkerControl)));
            renderer.fillColor(Color(1.0f, 0.85f, 0.3f, 0.8f));
            renderer.rect(waveX + marker * waveW - 1.0f * scale, waveY, 2.0f * scale, waveH);
            renderer.fill();
         }

         char line[48];
         snprintf(line, sizeof(line), "%.28s", mSampleName.empty() ? "Drop audio" : mSampleName.c_str());
         drawText(renderer, waveX, waveY + waveH + 12.0f * scale, line, UITheme::kTextPrimary, 10.0f * scale);
      }

      void SamplePanelModule::setStringProperty(const std::string& name, const std::string& value)
      {
         if (name == kSampleHashExtra)
         {
            mSampleHash = value;
            refreshSample();
         }
      }

      std::string SamplePanelModule::getStringProperty(const std::string& name) const
      {
         return name == kSampleHashExtra ? mSampleHash : std::string();
      }

      std::vector<std::string> SamplePanelModule::stringPropertyNames() const
      {
         return { kSampleHashExtra };
      }

   } // namespace wasm
} // namespace bespoke
