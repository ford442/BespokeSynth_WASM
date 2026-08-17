/**
 * BespokeSynth WASM - Sampler canvas module
 */

#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/SampleBuffer.h"
#include "BespokeWasm/SampleStore.h"
#include "BespokeWasm/Theme.h"
#include <algorithm>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      SamplerModule::SamplerModule(int id)
      : Module(id, "sampler", "Sampler", ModuleCategory::Synth)
      {
         setSize(200, 126);
      }

      void SamplerModule::refreshSample()
      {
         mSample = mSampleHash.empty() ? nullptr : SampleStore::instance().findByHash(mSampleHash);
         if (mSample)
            mSampleName = mSample->name();
      }

      void SamplerModule::render(Renderer2D& renderer, float offsetX, float offsetY, float scale)
      {
         Module::render(renderer, offsetX, offsetY, scale);

         const float screenX = (mX + offsetX) * scale;
         const float screenY = (mY + offsetY) * scale;
         const float waveX = screenX + 8 * scale;
         const float waveY = screenY + (kTitleBarHeight + 8) * scale;
         const float waveW = 184 * scale;
         const float waveH = 48 * scale;

         renderer.fillColor(Color(0.08f, 0.08f, 0.1f, 1.0f));
         renderer.rect(waveX, waveY, waveW, waveH);
         renderer.fill();

         if (mSample && !mSample->peaks().empty())
         {
            const auto& peaks = mSample->peaks();
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

         const float startX = waveX + mStart * waveW;
         const float endX = waveX + mEnd * waveW;
         renderer.fillColor(Color(1.0f, 0.85f, 0.3f, 0.18f));
         renderer.rect(startX, waveY, std::max(1.0f, endX - startX), waveH);
         renderer.fill();

         const char* modeName = mMode == 1 ? "Loop" : (mMode == 2 ? "Gate" : "One-shot");
         char line[64];
         if (mSampleName.empty())
            snprintf(line, sizeof(line), "Drop audio  %s", modeName);
         else
            snprintf(line, sizeof(line), "%.16s  %s", mSampleName.c_str(), modeName);
         drawText(renderer, waveX, waveY + waveH + 14 * scale, line, UITheme::kTextPrimary, 10.0f * scale);
      }

      void SamplerModule::setControlValue(const std::string& name, float value)
      {
         if (name == "volume")
            mVolume = value;
         else if (name == "start")
            mStart = value;
         else if (name == "end")
            mEnd = value;
         else if (name == "loopStart")
            mLoopStart = value;
         else if (name == "loopEnd")
            mLoopEnd = value;
         else if (name == "mode")
            mMode = static_cast<int>(value);
         else if (name == "root")
            mRootPitch = static_cast<int>(value);
      }

      float SamplerModule::getControlValue(const std::string& name) const
      {
         if (name == "volume")
            return mVolume;
         if (name == "start")
            return mStart;
         if (name == "end")
            return mEnd;
         if (name == "loopStart")
            return mLoopStart;
         if (name == "loopEnd")
            return mLoopEnd;
         if (name == "mode")
            return static_cast<float>(mMode);
         if (name == "root")
            return static_cast<float>(mRootPitch);
         return 0.0f;
      }

      void SamplerModule::setStringProperty(const std::string& name, const std::string& value)
      {
         if (name == "sampleHash")
         {
            mSampleHash = value;
            refreshSample();
         }
      }

      std::string SamplerModule::getStringProperty(const std::string& name) const
      {
         if (name == "sampleHash")
            return mSampleHash;
         return {};
      }

      std::vector<std::string> SamplerModule::stringPropertyNames() const
      {
         return { "sampleHash" };
      }

      bool SamplerModule::handleMouseDown(float worldX, float worldY)
      {
         const float waveX = mX + 8.0f;
         const float waveY = mY + kTitleBarHeight + 8.0f;
         const float waveW = 184.0f;
         const float waveH = 48.0f;
         if (worldX >= waveX && worldX <= waveX + waveW && worldY >= waveY && worldY <= waveY + waveH)
         {
            const float t = std::max(0.0f, std::min(1.0f, (worldX - waveX) / waveW));
            if (worldY < waveY + waveH * 0.5f)
            {
               mStart = t;
               if (mEnd <= mStart)
                  mEnd = std::min(1.0f, mStart + 0.05f);
            }
            else
            {
               mEnd = t;
               if (mEnd <= mStart)
                  mStart = std::max(0.0f, mEnd - 0.05f);
            }
            return true;
         }

         const float labelY = waveY + waveH + 4.0f;
         if (worldX >= waveX && worldX <= waveX + waveW && worldY >= labelY && worldY <= labelY + 16.0f)
         {
            mMode = (mMode + 1) % 3;
            return true;
         }
         return false;
      }

   } // namespace wasm
} // namespace bespoke
