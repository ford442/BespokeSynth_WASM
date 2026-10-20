/**
 * BespokeSynth WASM - Canvas MIDI CC map and MIDI learn
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <algorithm>
#include <array>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         struct NamedRange
         {
            const char* name;
            float minValue;
            float maxValue;
         };

         // Fallback ranges for MIDI learn on modules without a slider table.
         constexpr NamedRange kFallbackControlRanges[] = {
            { "cutoff", 20.0f, 20000.0f },
            { "frequency", 20.0f, 20000.0f },
            { "resonance", 0.0f, 1.0f },
            { "gain", 0.0f, 1.0f },
            { "volume", 0.0f, 1.0f },
            { "level", 0.0f, 1.0f },
            { "depth", 0.0f, 1.0f },
            { "mix", 0.0f, 1.0f },
            { "rate", 0.05f, 20.0f },
            { "attack", 1.0f, 2000.0f },
            { "decay", 1.0f, 2000.0f },
            { "sustain", 0.0f, 1.0f },
            { "release", 1.0f, 4000.0f },
            { "time", 0.01f, 2.0f },
            { "feedback", 0.0f, 0.95f },
            { "room", 0.0f, 1.0f },
            { "damping", 0.0f, 1.0f },
            { "pitch", 0.0f, 127.0f },
            { "gate", 0.05f, 1.0f },
            { "bpm", 40.0f, 240.0f },
            { "swing", 0.0f, 1.0f },
            { "root", 0.0f, 11.0f },
            { "waveform", 0.0f, 3.0f },
            { "shape", 0.0f, 3.0f },
            { "type", 0.0f, 2.0f },
         };
      }

      bool ModuleCanvas::applyMidiCc(int channel, int cc, float value)
      {
         Lock lock(mMutex);
         if (mMidiLearnModuleId >= 0)
         {
            // Learn binds this exact channel+CC; one control per CC and one CC per control.
            MidiCcBinding binding;
            binding.channel = channel;
            binding.cc = cc;
            binding.moduleId = mMidiLearnModuleId;
            binding.controlName = mMidiLearnControl;
            controlRangeFor(binding.moduleId, binding.controlName, binding.min, binding.max);
            mMidiCcMap.erase(std::remove_if(mMidiCcMap.begin(), mMidiCcMap.end(),
                                            [&binding](const MidiCcBinding& existing)
                                            {
                                               const bool sameControl = existing.moduleId == binding.moduleId &&
                                                                        existing.controlName == binding.controlName;
                                               const bool sameSource = existing.cc == binding.cc &&
                                                                       (existing.channel < 0 || existing.channel == binding.channel);
                                               return sameControl || sameSource;
                                            }),
                             mMidiCcMap.end());
            mMidiCcMap.push_back(binding);
            // A learned map is user data: stop re-seeding defaults on spawn/delete.
            mMidiMapFromPatch = true;
            printf("ModuleCanvas: MIDI learn ch%d CC%d -> %d.%s\n", channel, cc, binding.moduleId,
                   binding.controlName.c_str());
            cancelMidiLearn();
         }

         // At most a handful of bindings share one CC; no allocation per message.
         std::array<int, 8> touched{};
         size_t touchedCount = 0;
         bool overflow = false;
         forEachMatchingMidiCc(mMidiCcMap, channel, cc, value,
                               [&](int moduleId, const std::string& controlName, float scaled)
                               {
                                  auto it = mModules.find(moduleId);
                                  if (it == mModules.end())
                                     return;
                                  it->second->setControlValue(controlName, scaled);
                                  if (std::find(touched.begin(), touched.begin() + touchedCount, moduleId) !=
                                      touched.begin() + touchedCount)
                                     return;
                                  if (touchedCount < touched.size())
                                     touched[touchedCount++] = moduleId;
                                  else
                                     overflow = true;
                               });
         if (overflow)
            publishAudioGraphSnapshotLocked();
         else
         {
            for (size_t i = 0; i < touchedCount; ++i)
               syncModuleParamsLocked(touched[i]);
         }
         return touchedCount > 0;
      }

      int ModuleCanvas::midiNoteChannel() const
      {
         Lock lock(mMutex);
         return mMidiNoteChannel;
      }

      void ModuleCanvas::setMidiNoteChannel(int channel)
      {
         Lock lock(mMutex);
         mMidiNoteChannel = channel;
      }

      bool ModuleCanvas::armMidiLearn(int moduleId, const std::string& controlName)
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return false;
         std::string name = controlName;
         if (name.empty())
         {
            const auto names = WasmModuleAdapterRegistry::instance().serializableControlNames(it->second->getType());
            if (const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find(it->second->getType()))
            {
               const auto descriptors = adapter->controlDescriptors();
               if (!descriptors.empty())
                  name = descriptors.front().name;
            }
            if (name.empty() && !names.empty())
               name = names.front();
         }
         if (name.empty())
            return false;
         mMidiLearnModuleId = moduleId;
         mMidiLearnControl = name;
         printf("ModuleCanvas: MIDI learn armed for %d.%s\n", moduleId, name.c_str());
         return true;
      }

      bool ModuleCanvas::armMidiLearnAt(float screenX, float screenY)
      {
         Lock lock(mMutex);
         const float worldX = screenToWorldX(screenX);
         const float worldY = screenToWorldY(screenY - kTitleBarHeight);
         for (const auto& [id, module] : mModules)
         {
            if (!module->hitTest(worldX, worldY))
               continue;
            return armMidiLearn(id, module->controlNameAt(worldX, worldY));
         }
         return false;
      }

      void ModuleCanvas::cancelMidiLearn()
      {
         Lock lock(mMutex);
         mMidiLearnModuleId = -1;
         mMidiLearnControl.clear();
      }

      bool ModuleCanvas::isMidiLearnArmed() const
      {
         Lock lock(mMutex);
         return mMidiLearnModuleId >= 0;
      }

      int ModuleCanvas::midiLearnModuleId() const
      {
         Lock lock(mMutex);
         return mMidiLearnModuleId;
      }

      std::string ModuleCanvas::midiLearnControl() const
      {
         Lock lock(mMutex);
         return mMidiLearnControl;
      }

      bool ModuleCanvas::removeMidiCcBinding(int moduleId, const std::string& controlName)
      {
         Lock lock(mMutex);
         const size_t before = mMidiCcMap.size();
         mMidiCcMap.erase(std::remove_if(mMidiCcMap.begin(), mMidiCcMap.end(),
                                         [&](const MidiCcBinding& binding)
                                         {
                                            return binding.moduleId == moduleId &&
                                                   (controlName.empty() || binding.controlName == controlName);
                                         }),
                          mMidiCcMap.end());
         if (mMidiCcMap.size() == before)
            return false;
         mMidiMapFromPatch = true;
         return true;
      }

      std::vector<MidiCcBinding> ModuleCanvas::midiCcMap() const
      {
         Lock lock(mMutex);
         return mMidiCcMap;
      }

      void ModuleCanvas::controlRangeFor(int moduleId, const std::string& name, float& minValue, float& maxValue) const
      {
         Lock lock(mMutex);
         minValue = 0.0f;
         maxValue = 1.0f;
         auto it = mModules.find(moduleId);
         if (it != mModules.end() && it->second->controlRange(name, minValue, maxValue))
            return;
         for (const auto& range : kFallbackControlRanges)
         {
            if (name == range.name)
            {
               minValue = range.minValue;
               maxValue = range.maxValue;
               return;
            }
         }
      }

      void ModuleCanvas::seedDefaultMidiCcMapLocked()
      {
         auto findType = [this](const char* type) -> int
         {
            for (const auto& [id, module] : mModules)
            {
               if (module->getType() == type)
                  return id;
            }
            return -1;
         };

         mMidiCcMap.clear();
         const int gain = findType("gain");
         if (gain >= 0)
            mMidiCcMap.push_back({ -1, 7, gain, "gain", 0.0f, 1.0f });
         const int filter = findType("filter");
         if (filter >= 0)
            mMidiCcMap.push_back({ -1, 74, filter, "cutoff", 20.0f, 20000.0f });
         const int lfo = findType("lfo");
         if (lfo >= 0)
            mMidiCcMap.push_back({ -1, 1, lfo, "depth", 0.0f, 1.0f });
      }

   } // namespace wasm
} // namespace bespoke
