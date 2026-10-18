/**
 * BespokeSynth WASM
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/ModuleFactory.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include "BespokeWasm/WasmPatchState.h"
#include "BespokeWasm/AudioProcessPlan.h"
#include "BespokeWasm/AudioAnalysis.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/PixelFont.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         // Consecutive API/control edits on the same control within this window
         // collapse into one undo step.
         constexpr unsigned long long kUndoCoalesceMs = 400;

         unsigned long long undoClockMs()
         {
            using namespace std::chrono;
            return static_cast<unsigned long long>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
         }

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

      ModuleCanvas::ModuleCanvas()
      {
         setSpawnMenuTypeHidden("script", true);
         // Create persistent Transport module
         auto transport = std::make_unique<TransportModule>(mNextModuleId++);
         transport->setPosition(20, 50);
         mTransport = transport.get();
         mModules[mTransport->getId()] = std::move(transport);

         // Create persistent Scale module
         auto scaleModule = std::make_unique<ScaleModule>(mNextModuleId++);
         scaleModule->setPosition(290, 50);
         mScaleModule = scaleModule.get();
         mModules[mScaleModule->getId()] = std::move(scaleModule);

         Lock lock(mMutex);
         seedDefaultMidiCcMapLocked();
         publishAudioGraphSnapshotLocked();
         printf("ModuleCanvas: Created with Transport and Scale modules\n");
      }

      int ModuleCanvas::createModule(const std::string& type, float x, float y)
      {
         Lock lock(mMutex);
         const int id = createModuleWithIdLocked(type, x, y, mNextModuleId);
         if (id >= 0 && mUndoSuppressDepth == 0)
         {
            UndoCommand command;
            command.kind = UndoCommand::Kind::AddModule;
            command.module = captureModuleLocked(*mModules[id]);
            recordLocked(std::move(command));
         }
         return id;
      }

      int ModuleCanvas::createModuleWithIdLocked(const std::string& type, float x, float y, int id)
      {
         if (id < 0 || mModules.count(id) != 0)
            return -1;
         auto module = ModuleFactory::instance().createModule(type, id);
         if (!module)
         {
            printf("ModuleCanvas: Unknown module type '%s'\n", type.c_str());
            return -1;
         }

         mNextModuleId = std::max(mNextModuleId, id + 1);
         module->setPosition(x, y);
         printf("ModuleCanvas: Created module '%s' (id=%d) at (%.1f, %.1f)\n",
                type.c_str(), id, x, y);
         mModules[id] = std::move(module);
         if (!mMidiMapFromPatch)
            seedDefaultMidiCcMapLocked();
         publishAudioGraphSnapshotLocked();
         return id;
      }

      void ModuleCanvas::deleteModule(int moduleId)
      {
         Lock lock(mMutex);
         // Don't allow deleting Transport or Scale
         if (mTransport && moduleId == mTransport->getId())
            return;
         if (mScaleModule && moduleId == mScaleModule->getId())
            return;
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return;

         if (mUndoSuppressDepth == 0)
         {
            UndoCommand command;
            command.kind = UndoCommand::Kind::RemoveModule;
            command.module = captureModuleLocked(*it->second);
            for (const auto& conn : mConnections)
            {
               if (conn.sourceModuleId == moduleId || conn.destModuleId == moduleId)
                  command.connections.push_back({ conn.sourceModuleId, conn.sourcePortIndex,
                                                  conn.destModuleId, conn.destPortIndex });
            }
            for (const auto& binding : mMidiCcMap)
            {
               if (binding.moduleId == moduleId)
                  command.bindings.push_back(binding);
            }
            recordLocked(std::move(command));
         }
         deleteModuleLocked(moduleId);
      }

      void ModuleCanvas::deleteModuleLocked(int moduleId)
      {
         // Remove connections involving this module
         mConnections.erase(
         std::remove_if(mConnections.begin(), mConnections.end(),
                        [moduleId](const Connection& c)
                        {
                           return c.sourceModuleId == moduleId || c.destModuleId == moduleId;
                        }),
         mConnections.end());
         mMidiCcMap.erase(std::remove_if(mMidiCcMap.begin(), mMidiCcMap.end(),
                                         [moduleId](const MidiCcBinding& binding)
                                         {
                                            return binding.moduleId == moduleId;
                                         }),
                          mMidiCcMap.end());
         if (mMidiLearnModuleId == moduleId)
            cancelMidiLearn();
         if (mDraggedModuleId == moduleId)
            mDraggedModuleId = -1;
         if (mControlModuleId == moduleId)
            mControlModuleId = -1;

         mModules.erase(moduleId);
         printf("ModuleCanvas: Deleted module %d\n", moduleId);
         if (!mMidiMapFromPatch)
            seedDefaultMidiCcMapLocked();
         publishAudioGraphSnapshotLocked();
      }

      Module* ModuleCanvas::getModule(int moduleId)
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it != mModules.end())
            return it->second.get();
         return nullptr;
      }

      int ModuleCanvas::findFirstModuleOfType(const std::string& type) const
      {
         Lock lock(mMutex);
         for (const auto& [id, module] : mModules)
         {
            if (module->getType() == type)
               return id;
         }
         return -1;
      }

      bool ModuleCanvas::setModuleControlValue(int moduleId, const std::string& name, float value, bool recordUndo)
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return false;
         const float before = it->second->getControlValue(name);
         it->second->setControlValue(name, value);
         const float after = it->second->getControlValue(name);
         if (recordUndo && before != after)
            recordLocked(UndoCommand::setControl(moduleId, name, before, after), true);
         publishAudioGraphSnapshotLocked();
         return true;
      }

      bool ModuleCanvas::getModuleControlValue(int moduleId, const std::string& name, float& value) const
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return false;
         value = it->second->getControlValue(name);
         return true;
      }

      bool ModuleCanvas::setModuleStringProperty(int moduleId, const std::string& name, const std::string& value)
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return false;
         it->second->setStringProperty(name, value);
         publishAudioGraphSnapshotLocked();
         return true;
      }

      bool ModuleCanvas::getModuleStringProperty(int moduleId, const std::string& name, std::string& value) const
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return false;
         value = it->second->getStringProperty(name);
         return true;
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

         bool applied = false;
         forEachMatchingMidiCc(mMidiCcMap, channel, cc, value,
                               [&](int moduleId, const std::string& controlName, float scaled)
                               {
                                  auto it = mModules.find(moduleId);
                                  if (it == mModules.end())
                                     return;
                                  it->second->setControlValue(controlName, scaled);
                                  applied = true;
                               });
         if (applied)
            publishAudioGraphSnapshotLocked();
         return applied;
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

      void ModuleCanvas::setScriptingEnabled(bool enabled)
      {
         Lock lock(mMutex);
         mScriptingEnabled = enabled;
         setSpawnMenuTypeHidden("script", !enabled);
      }

      bool ModuleCanvas::isScriptingEnabled() const
      {
         Lock lock(mMutex);
         return mScriptingEnabled;
      }

      bool ModuleCanvas::hasOutgoingConnection(int moduleId) const
      {
         Lock lock(mMutex);
         return std::any_of(mConnections.begin(), mConnections.end(),
                            [moduleId](const Connection& conn)
                            {
                               return conn.sourceModuleId == moduleId;
                            });
      }

      void ModuleCanvas::setSnapshotListener(SnapshotListener listener)
      {
         Lock lock(mMutex);
         mSnapshotListener = std::move(listener);
         if (mSnapshotListener && mPublishedAudioGraph)
            mSnapshotListener(*mPublishedAudioGraph);
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

      void ModuleCanvas::connectModules(int sourceId, int sourcePort, int destId, int destPort)
      {
         Lock lock(mMutex);
         if (connectLocked(sourceId, sourcePort, destId, destPort))
            recordLocked(UndoCommand::cable(UndoCommand::Kind::Connect,
                                            { sourceId, sourcePort, destId, destPort }));
      }

      bool ModuleCanvas::connectLocked(int sourceId, int sourcePort, int destId, int destPort)
      {
         if (!portsAreCompatible(sourceId, sourcePort, destId, destPort))
         {
            printf("ModuleCanvas: Rejected incompatible connection %d:%d -> %d:%d\n",
                   sourceId, sourcePort, destId, destPort);
            return false;
         }
         for (const auto& existing : mConnections)
         {
            if (existing.sourceModuleId == sourceId && existing.sourcePortIndex == sourcePort &&
                existing.destModuleId == destId && existing.destPortIndex == destPort)
               return false;
         }
         Connection conn;
         conn.sourceModuleId = sourceId;
         conn.sourcePortIndex = sourcePort;
         conn.destModuleId = destId;
         conn.destPortIndex = destPort;

         // Color based on port type
         Module* srcMod = getModule(sourceId);
         if (srcMod && sourcePort < static_cast<int>(srcMod->getOutputs().size()))
         {
            switch (srcMod->getOutputs()[sourcePort].type)
            {
               case PortType::Audio:
                  conn.color = Color(0.3f, 0.7f, 0.9f, 0.9f);
                  break;
               case PortType::Note:
                  conn.color = Color(0.9f, 0.7f, 0.3f, 0.9f);
                  break;
               case PortType::Pulse:
                  conn.color = Color(0.9f, 0.3f, 0.3f, 0.9f);
                  break;
               case PortType::Modulation:
                  conn.color = Color(0.5f, 0.9f, 0.4f, 0.9f);
                  break;
            }
         }
         else
         {
            conn.color = Color(0.5f, 0.5f, 0.55f, 0.9f);
         }

         mConnections.push_back(conn);
         printf("ModuleCanvas: Connected %d:%d -> %d:%d\n", sourceId, sourcePort, destId, destPort);
         publishAudioGraphSnapshotLocked();
         return true;
      }

      void ModuleCanvas::disconnectModules(int sourceId, int destId)
      {
         Lock lock(mMutex);
         UndoCommand group;
         group.kind = UndoCommand::Kind::Group;
         for (const auto& conn : mConnections)
         {
            if (conn.sourceModuleId == sourceId && conn.destModuleId == destId)
               group.children.push_back(UndoCommand::cable(
               UndoCommand::Kind::Disconnect,
               { conn.sourceModuleId, conn.sourcePortIndex, conn.destModuleId, conn.destPortIndex }));
         }
         mConnections.erase(
         std::remove_if(mConnections.begin(), mConnections.end(),
                        [sourceId, destId](const Connection& c)
                        {
                           return c.sourceModuleId == sourceId && c.destModuleId == destId;
                        }),
         mConnections.end());
         if (group.children.size() == 1)
            recordLocked(std::move(group.children.front()));
         else if (!group.children.empty())
            recordLocked(std::move(group));
         publishAudioGraphSnapshotLocked();
      }

      std::shared_ptr<const ModuleCanvas::AudioGraphSnapshot> ModuleCanvas::getAudioGraphSnapshot() const
      {
         return std::atomic_load_explicit(&mPublishedAudioGraph, std::memory_order_acquire);
      }

      void ModuleCanvas::buildAudioGraphSnapshotLocked(AudioGraphSnapshot& snapshot) const
      {
         snapshot.transportPlaying = mTransport && mTransport->isPlaying();
         snapshot.transportBPM = mTransport ? mTransport->getBPM() : 120.0f;
         if (mScaleModule)
         {
            snapshot.musicScale.root = static_cast<int>(mScaleModule->getControlValue("root"));
            snapshot.musicScale.mode = static_cast<int>(mScaleModule->getControlValue("type"));
            snapshot.musicScale.customTuning = mScaleModule->hasCustomTuning();
            snapshot.musicScale.noteFrequency = mScaleModule->tuningTable();
         }
         snapshot.nodes.clear();
         snapshot.connections.clear();
         snapshot.paramArena.clear();
         snapshot.nodes.reserve(mModules.size());
         snapshot.connections.reserve(mConnections.size());

         auto& registry = WasmModuleAdapterRegistry::instance();
         for (const auto& [id, module] : mModules)
         {
            appendAudioGraphNode(snapshot, id, module->getType(), module->isEnabled(),
                                 moduleControlMap(*module), moduleStringMap(*module));
         }

         for (const auto& conn : mConnections)
         {
            auto srcIt = mModules.find(conn.sourceModuleId);
            auto dstIt = mModules.find(conn.destModuleId);
            if (srcIt == mModules.end() || dstIt == mModules.end())
               continue;

            const WasmModuleAdapter* srcAdapter = registry.find(srcIt->second->getType());
            const WasmModuleAdapter* dstAdapter = registry.find(dstIt->second->getType());
            if (!srcAdapter || !dstAdapter)
               continue;

            const auto outputs = srcAdapter->outputPorts();
            const auto inputs = dstAdapter->inputPorts();
            if (conn.sourcePortIndex < 0 || conn.destPortIndex < 0 ||
                conn.sourcePortIndex >= static_cast<int>(outputs.size()) ||
                conn.destPortIndex >= static_cast<int>(inputs.size()))
               continue;

            AudioGraphConnection audioConn;
            audioConn.sourceModuleId = conn.sourceModuleId;
            audioConn.sourcePortIndex = conn.sourcePortIndex;
            audioConn.destModuleId = conn.destModuleId;
            audioConn.destPortIndex = conn.destPortIndex;
            audioConn.sourcePortType = outputs[static_cast<size_t>(conn.sourcePortIndex)].type;
            audioConn.destPortType = inputs[static_cast<size_t>(conn.destPortIndex)].type;
            snapshot.connections.push_back(audioConn);
         }
      }

      std::map<std::string, float> ModuleCanvas::moduleControlMap(const Module& module) const
      {
         std::map<std::string, float> controls;
         for (const auto& name : WasmModuleAdapterRegistry::instance().serializableControlNames(module.getType()))
            controls[name] = module.getControlValue(name);
         return controls;
      }

      std::map<std::string, std::string> ModuleCanvas::moduleStringMap(const Module& module) const
      {
         std::map<std::string, std::string> extras;
         for (const auto& name : module.stringPropertyNames())
            extras[name] = module.getStringProperty(name);
         return extras;
      }

      void ModuleCanvas::publishAudioGraphSnapshotLocked()
      {
         AudioGraphSnapshot snapshot;
         buildAudioGraphSnapshotLocked(snapshot);
         compileAudioProcessPlan(snapshot, snapshot.processPlan);
         auto published = std::make_shared<const AudioGraphSnapshot>(std::move(snapshot));
         if (mSnapshotListener)
            mSnapshotListener(*published);
         std::atomic_store_explicit(&mPublishedAudioGraph, std::move(published), std::memory_order_release);
      }

      bool ModuleCanvas::portsAreCompatible(int sourceId, int sourcePort, int destId, int destPort) const
      {
         auto sourceIt = mModules.find(sourceId);
         auto destIt = mModules.find(destId);
         if (sourceIt == mModules.end() || destIt == mModules.end() || sourceId == destId ||
             sourcePort < 0 || destPort < 0)
            return false;

         auto& registry = WasmModuleAdapterRegistry::instance();
         const WasmModuleAdapter* sourceAdapter = registry.find(sourceIt->second->getType());
         const WasmModuleAdapter* destAdapter = registry.find(destIt->second->getType());
         if (!sourceAdapter || !destAdapter)
            return false;

         const auto outputs = sourceAdapter->outputPorts();
         const auto inputs = destAdapter->inputPorts();
         return sourcePort < static_cast<int>(outputs.size()) && destPort < static_cast<int>(inputs.size()) &&
                outputs[static_cast<size_t>(sourcePort)].type == inputs[static_cast<size_t>(destPort)].type;
      }

      bool ModuleCanvas::findPortAt(float worldX, float worldY, bool output, int& moduleId, int& portIndex) const
      {
         const float hitRadius = Module::kPortRadius + 4.0f;
         for (const auto& [id, module] : mModules)
         {
            const auto& ports = output ? module->getOutputs() : module->getInputs();
            const float portX = module->getX() + (output ? module->getWidth() : 0.0f);
            for (size_t index = 0; index < ports.size(); ++index)
            {
               const float portY = module->getY() + Module::kTitleBarHeight + 10.0f + index * 15.0f;
               const float dx = worldX - portX;
               const float dy = worldY - portY;
               if (dx * dx + dy * dy <= hitRadius * hitRadius)
               {
                  moduleId = id;
                  portIndex = static_cast<int>(index);
                  return true;
               }
            }
         }
         return false;
      }

      bool ModuleCanvas::removeConnectionAt(float screenX, float screenY)
      {
         const float canvasTop = kTitleBarHeight;
         const float threshold = 8.0f;
         for (auto it = mConnections.begin(); it != mConnections.end(); ++it)
         {
            auto sourceIt = mModules.find(it->sourceModuleId);
            auto destIt = mModules.find(it->destModuleId);
            if (sourceIt == mModules.end() || destIt == mModules.end())
               continue;
            const Module& source = *sourceIt->second;
            const Module& dest = *destIt->second;
            const float x1 = (source.getX() + source.getWidth() + mOffsetX) * mScale;
            const float y1 = (source.getY() + Module::kTitleBarHeight + 10.0f + it->sourcePortIndex * 15.0f + mOffsetY) * mScale + canvasTop;
            const float x2 = (dest.getX() + mOffsetX) * mScale;
            const float y2 = (dest.getY() + Module::kTitleBarHeight + 10.0f + it->destPortIndex * 15.0f + mOffsetY) * mScale + canvasTop;
            const float dx = x2 - x1;
            const float dy = y2 - y1;
            const float lengthSquared = dx * dx + dy * dy;
            const float t = lengthSquared > 0.0f
                            ? std::max(0.0f, std::min(1.0f, ((screenX - x1) * dx + (screenY - y1) * dy) / lengthSquared))
                            : 0.0f;
            const float px = x1 + t * dx;
            const float py = y1 + t * dy;
            const float distanceX = screenX - px;
            const float distanceY = screenY - py;
            if (distanceX * distanceX + distanceY * distanceY <= threshold * threshold)
            {
               recordLocked(UndoCommand::cable(
               UndoCommand::Kind::Disconnect,
               { it->sourceModuleId, it->sourcePortIndex, it->destModuleId, it->destPortIndex }));
               mConnections.erase(it);
               return true;
            }
         }
         return false;
      }

      ModuleCanvas::AudioGraphSnapshot ModuleCanvas::createAudioGraphSnapshot() const
      {
         Lock lock(mMutex);
         AudioGraphSnapshot snapshot;
         buildAudioGraphSnapshotLocked(snapshot);
         compileAudioProcessPlan(snapshot, snapshot.processPlan);
         return snapshot;
      }

      ModuleCanvas::StateSnapshot ModuleCanvas::createStateSnapshot() const
      {
         Lock lock(mMutex);

         StateSnapshot snapshot;
         snapshot.schemaVersion = kPatchSchemaVersion;
         snapshot.transportBPM = mTransport ? mTransport->getBPM() : 120.0f;
         snapshot.transportPlaying = mTransport && mTransport->isPlaying();
         snapshot.offsetX = mOffsetX;
         snapshot.offsetY = mOffsetY;
         snapshot.scale = mScale;
         snapshot.midiCcMap = mMidiCcMap;
         snapshot.midiNoteChannel = mMidiNoteChannel;
         snapshot.modules.reserve(mModules.size());
         snapshot.connections.reserve(mConnections.size());

         for (const auto& [id, module] : mModules)
         {
            StateModule stateModule;
            stateModule.id = id;
            stateModule.type = module->getType();
            stateModule.x = module->getX();
            stateModule.y = module->getY();
            stateModule.minimized = module->isMinimized();
            stateModule.enabled = module->isEnabled();

            for (const auto& controlName : WasmModuleAdapterRegistry::instance().serializableControlNames(stateModule.type))
               stateModule.controls[controlName] = module->getControlValue(controlName);
            for (const auto& extraName : module->stringPropertyNames())
               stateModule.extras[extraName] = module->getStringProperty(extraName);

            snapshot.modules.push_back(stateModule);
         }

         for (const auto& conn : mConnections)
         {
            StateConnection stateConn;
            stateConn.sourceModuleId = conn.sourceModuleId;
            stateConn.sourcePortIndex = conn.sourcePortIndex;
            stateConn.destModuleId = conn.destModuleId;
            stateConn.destPortIndex = conn.destPortIndex;
            snapshot.connections.push_back(stateConn);
         }

         return snapshot;
      }

      bool ModuleCanvas::applyStateSnapshot(const StateSnapshot& snapshot)
      {
         Lock lock(mMutex);
         UndoSuppressor suppress(mUndoSuppressDepth);
         std::unordered_map<int, int> idMap;
         mMidiMapFromPatch = true;
         cancelMidiLearn();

         clearUserModules();
         setViewTransform(snapshot.offsetX, snapshot.offsetY, snapshot.scale);
         if (mScaleModule)
         {
            // A patch without Scala extras is 12-TET.
            for (const char* key : { "scl", "sclName", "kbm", "kbmName" })
               mScaleModule->setStringProperty(key, "");
         }

         if (mTransport)
         {
            mTransport->setBPM(snapshot.transportBPM);
            mTransport->setPlaying(snapshot.transportPlaying);
         }

         for (const auto& stateModule : snapshot.modules)
         {
            Module* module = nullptr;
            int newId = -1;

            if (stateModule.type == "transport")
            {
               module = mTransport;
               newId = module ? module->getId() : -1;
            }
            else if (stateModule.type == "scale")
            {
               module = mScaleModule;
               newId = module ? module->getId() : -1;
            }
            else
            {
               newId = createModule(stateModule.type, stateModule.x, stateModule.y);
               module = getModule(newId);
            }

            if (!module || newId < 0)
               continue;

            idMap[stateModule.id] = newId;
            module->setPosition(stateModule.x, stateModule.y);
            module->setMinimized(stateModule.minimized);
            module->setEnabled(stateModule.enabled);

            for (const auto& [name, value] : stateModule.controls)
               module->setControlValue(name, value);
            for (const auto& [name, value] : stateModule.extras)
               module->setStringProperty(name, value);
         }

         for (const auto& stateConn : snapshot.connections)
         {
            auto srcIt = idMap.find(stateConn.sourceModuleId);
            auto dstIt = idMap.find(stateConn.destModuleId);
            if (srcIt == idMap.end() || dstIt == idMap.end())
               continue;
            connectModules(srcIt->second, stateConn.sourcePortIndex, dstIt->second, stateConn.destPortIndex);
         }

         mMidiNoteChannel = snapshot.midiNoteChannel;
         mMidiCcMap.clear();
         for (auto binding : snapshot.midiCcMap)
         {
            auto idIt = idMap.find(binding.moduleId);
            if (idIt == idMap.end())
               continue;
            binding.moduleId = idIt->second;
            mMidiCcMap.push_back(std::move(binding));
         }

         mUndoStack.clear();
         publishAudioGraphSnapshotLocked();
         return true;
      }

      void ModuleCanvas::zoom(float factor, float centerX, float centerY)
      {
         Lock lock(mMutex);
         float worldCenterX = screenToWorldX(centerX);
         float worldCenterY = screenToWorldY(centerY);

         mScale *= factor;
         mScale = std::max(0.25f, std::min(4.0f, mScale));

         // Adjust offset to keep the zoom centered
         mOffsetX = centerX / mScale - worldCenterX;
         mOffsetY = centerY / mScale - worldCenterY;
      }

      float ModuleCanvas::screenToWorldX(float screenX) const
      {
         return screenX / mScale - mOffsetX;
      }

      float ModuleCanvas::screenToWorldY(float screenY) const
      {
         return screenY / mScale - mOffsetY;
      }

      void ModuleCanvas::setOutputMeter(float peakLeft, float peakRight)
      {
         Lock lock(mMutex);
         for (auto& [id, module] : mModules)
         {
            if (module->getType() == "output")
            {
               module->setControlValue("meterL", peakLeft);
               module->setControlValue("meterR", peakRight);
               break;
            }
         }
      }

      void ModuleCanvas::clearUserModules()
      {
         Lock lock(mMutex);
         UndoSuppressor suppress(mUndoSuppressDepth);
         std::vector<int> toDelete;
         for (const auto& [id, module] : mModules)
         {
            if (mTransport && id == mTransport->getId())
               continue;
            if (mScaleModule && id == mScaleModule->getId())
               continue;
            toDelete.push_back(id);
         }
         for (int id : toDelete)
            deleteModule(id);
      }

      void ModuleCanvas::setViewTransform(float offsetX, float offsetY, float scale)
      {
         Lock lock(mMutex);
         mOffsetX = offsetX;
         mOffsetY = offsetY;
         mScale = std::max(0.25f, std::min(4.0f, scale));
      }

      void ModuleCanvas::setupCanonicalRenderTestScene()
      {
         Lock lock(mMutex);
         UndoSuppressor suppress(mUndoSuppressDepth);
         mUndoStack.clear();
         clearUserModules();
         setViewTransform(0.0f, 80.0f, 1.0f);

         if (mTransport)
         {
            mTransport->setPosition(20.0f, 50.0f);
            mTransport->setBPM(128.0f);
            mTransport->setPlaying(false);
            mTransport->setControlValue("swing", 0.15f);
         }
         if (mScaleModule)
         {
            mScaleModule->setPosition(290.0f, 50.0f);
            mScaleModule->setControlValue("root", 0.0f);
            mScaleModule->setControlValue("type", 0.0f);
         }

         const int seqId = createModule("stepsequencer", 80.0f, 310.0f);
         const int oscId = createModule("oscillator", 280.0f, 160.0f);
         const int filterId = createModule("filter", 470.0f, 170.0f);
         const int gainId = createModule("gain", 650.0f, 180.0f);
         const int lfoId = createModule("lfo", 280.0f, 310.0f);
         const int outputId = createModule("output", 820.0f, 190.0f);

         if (auto* seq = getModule(seqId))
         {
            seq->setControlValue("pattern", static_cast<float>(0x1111));
            seq->setControlValue("pitch", 60.0f);
            seq->setControlValue("gate", 0.8f);
         }
         if (auto* osc = getModule(oscId))
         {
            osc->setControlValue("frequency", 440.0f);
            osc->setControlValue("volume", 0.75f);
            osc->setControlValue("waveform", 0.0f);
         }
         if (auto* filter = getModule(filterId))
         {
            filter->setControlValue("cutoff", 1800.0f);
            filter->setControlValue("resonance", 0.65f);
            filter->setControlValue("type", 0.0f);
         }
         if (auto* gain = getModule(gainId))
            gain->setControlValue("gain", 0.7f);
         if (auto* lfo = getModule(lfoId))
         {
            lfo->setControlValue("rate", 2.5f);
            lfo->setControlValue("depth", 0.6f);
            lfo->setControlValue("shape", 0.0f);
         }

         if (seqId > 0 && oscId > 0)
            connectModules(seqId, 0, oscId, 0);
         if (oscId > 0 && filterId > 0)
            connectModules(oscId, 0, filterId, 0);
         if (filterId > 0 && gainId > 0)
            connectModules(filterId, 0, gainId, 0);
         if (gainId > 0 && outputId > 0)
            connectModules(gainId, 0, outputId, 0);
         if (lfoId > 0 && filterId > 0)
            connectModules(lfoId, 0, filterId, 1);

         publishAudioGraphSnapshotLocked();
      }

      void ModuleCanvas::setupDrumRenderTestScene()
      {
         Lock lock(mMutex);
         UndoSuppressor suppress(mUndoSuppressDepth);
         mUndoStack.clear();
         clearUserModules();
         setViewTransform(0.0f, 80.0f, 1.0f);

         if (mTransport)
         {
            mTransport->setPosition(20.0f, 50.0f);
            mTransport->setBPM(120.0f);
            mTransport->setPlaying(false);
            mTransport->setControlValue("swing", 0.0f);
         }
         if (mScaleModule)
         {
            mScaleModule->setPosition(290.0f, 50.0f);
            mScaleModule->setControlValue("root", 0.0f);
            mScaleModule->setControlValue("type", 0.0f);
         }

         const int drumsId = createModule("drumseq", 20.0f, 170.0f);
         const int noiseId = createModule("noise", 20.0f, 340.0f);
         const int kickId = createModule("oscillator", 290.0f, 160.0f);
         const int snareId = createModule("adsr", 290.0f, 290.0f);
         const int hatId = createModule("adsr", 290.0f, 420.0f);
         const int mixerId = createModule("mixer", 500.0f, 190.0f);
         const int compId = createModule("compressor", 690.0f, 190.0f);
         const int outputId = createModule("output", 900.0f, 210.0f);

         if (auto* drums = getModule(drumsId))
         {
            drums->setControlValue("kick", static_cast<float>(0x1111)); // four on the floor
            drums->setControlValue("snare", static_cast<float>(0x1010));
            drums->setControlValue("hat", static_cast<float>(0x4444)); // off-beats
            drums->setControlValue("tom", 0.0f);
            drums->setControlValue("kickpitch", 33.0f);
            drums->setControlValue("gate", 0.4f);
         }
         if (auto* noise = getModule(noiseId))
            noise->setControlValue("volume", 0.6f);
         if (auto* kick = getModule(kickId))
         {
            kick->setControlValue("frequency", 55.0f);
            kick->setControlValue("volume", 0.9f);
            kick->setControlValue("waveform", 0.0f);
         }
         if (auto* snare = getModule(snareId))
         {
            snare->setControlValue("attack", 1.0f);
            snare->setControlValue("decay", 90.0f);
            snare->setControlValue("sustain", 0.0f);
            snare->setControlValue("release", 40.0f);
         }
         if (auto* hat = getModule(hatId))
         {
            hat->setControlValue("attack", 1.0f);
            hat->setControlValue("decay", 25.0f);
            hat->setControlValue("sustain", 0.0f);
            hat->setControlValue("release", 15.0f);
         }
         if (auto* mixer = getModule(mixerId))
         {
            mixer->setControlValue("level1", 0.9f);
            mixer->setControlValue("level2", 0.6f);
            mixer->setControlValue("level3", 0.35f);
            mixer->setControlValue("master", 0.9f);
         }
         if (auto* comp = getModule(compId))
         {
            comp->setControlValue("threshold", -18.0f);
            comp->setControlValue("ratio", 4.0f);
            comp->setControlValue("makeup", 3.0f);
         }

         if (drumsId > 0)
         {
            connectModules(drumsId, 0, kickId, 0); // Kick → oscillator Pitch
            connectModules(drumsId, 1, snareId, 1); // Snare → ADSR Gate
            connectModules(drumsId, 2, hatId, 1); // Hat → ADSR Gate
            if (mTransport)
               connectModules(mTransport->getId(), 0, drumsId, 0);
         }
         connectModules(noiseId, 0, snareId, 0);
         connectModules(noiseId, 0, hatId, 0);
         connectModules(kickId, 0, mixerId, 0);
         connectModules(snareId, 0, mixerId, 1);
         connectModules(hatId, 0, mixerId, 2);
         connectModules(mixerId, 0, compId, 0);
         connectModules(compId, 0, outputId, 0);

         publishAudioGraphSnapshotLocked();
      }

      void ModuleCanvas::openSpawnMenu(float x, float y)
      {
         Lock lock(mMutex);
         mSpawnMenuOpen = true;
         mSpawnMenuX = x;
         mSpawnMenuY = y;
         mSpawnMenuSearch.clear();
         mSpawnMenuSelectedIndex = 0;
      }

      void ModuleCanvas::closeSpawnMenu()
      {
         Lock lock(mMutex);
         mSpawnMenuOpen = false;
         mSpawnMenuCategory = -1;
      }

      bool ModuleCanvas::isTransportPlaying() const
      {
         Lock lock(mMutex);
         return mTransport && mTransport->isPlaying();
      }

      void ModuleCanvas::setTransportPlaying(bool playing)
      {
         Lock lock(mMutex);
         if (mTransport)
            mTransport->setPlaying(playing);
         publishAudioGraphSnapshotLocked();
      }

      void ModuleCanvas::toggleTransportPlaying()
      {
         Lock lock(mMutex);
         if (mTransport)
            mTransport->setPlaying(!mTransport->isPlaying());
         publishAudioGraphSnapshotLocked();
      }

      float ModuleCanvas::getTransportBPM() const
      {
         Lock lock(mMutex);
         return mTransport ? mTransport->getBPM() : 120.0f;
      }

      void ModuleCanvas::setTransportBPM(float bpm)
      {
         Lock lock(mMutex);
         if (mTransport)
            mTransport->setBPM(bpm);
         publishAudioGraphSnapshotLocked();
      }

      UndoModuleState ModuleCanvas::captureModuleLocked(const Module& module) const
      {
         UndoModuleState state;
         state.id = module.getId();
         state.type = module.getType();
         state.x = module.getX();
         state.y = module.getY();
         state.minimized = module.isMinimized();
         state.enabled = module.isEnabled();
         state.controls = moduleControlMap(module);
         state.extras = moduleStringMap(module);
         return state;
      }

      void ModuleCanvas::restoreModuleLocked(const UndoModuleState& state)
      {
         if (createModuleWithIdLocked(state.type, state.x, state.y, state.id) < 0)
            return;
         Module* module = mModules[state.id].get();
         module->setMinimized(state.minimized);
         module->setEnabled(state.enabled);
         for (const auto& [name, value] : state.controls)
            module->setControlValue(name, value);
         for (const auto& [name, value] : state.extras)
            module->setStringProperty(name, value);
      }

      void ModuleCanvas::recordLocked(UndoCommand command, bool coalesce)
      {
         if (mUndoSuppressDepth > 0)
            return;
         command.stamp = undoClockMs();
         mUndoStack.push(std::move(command), coalesce ? kUndoCoalesceMs : 0);
      }

      void ModuleCanvas::applyCommandLocked(const UndoCommand& command, bool forward)
      {
         auto removeConnection = [this](const UndoConnection& c)
         {
            mConnections.erase(std::remove_if(mConnections.begin(), mConnections.end(),
                                              [&c](const Connection& conn)
                                              {
                                                 return conn.sourceModuleId == c.sourceModuleId &&
                                                        conn.sourcePortIndex == c.sourcePortIndex &&
                                                        conn.destModuleId == c.destModuleId &&
                                                        conn.destPortIndex == c.destPortIndex;
                                              }),
                               mConnections.end());
         };

         switch (command.kind)
         {
            case UndoCommand::Kind::AddModule:
               if (forward)
                  restoreModuleLocked(command.module);
               else
                  deleteModuleLocked(command.module.id);
               break;
            case UndoCommand::Kind::RemoveModule:
               if (forward)
               {
                  deleteModuleLocked(command.module.id);
                  break;
               }
               restoreModuleLocked(command.module);
               for (const auto& c : command.connections)
                  connectLocked(c.sourceModuleId, c.sourcePortIndex, c.destModuleId, c.destPortIndex);
               for (const auto& binding : command.bindings)
               {
                  const bool present = std::any_of(mMidiCcMap.begin(), mMidiCcMap.end(),
                                                   [&binding](const MidiCcBinding& existing)
                                                   {
                                                      return existing.moduleId == binding.moduleId &&
                                                             existing.controlName == binding.controlName &&
                                                             existing.cc == binding.cc;
                                                   });
                  if (!present)
                     mMidiCcMap.push_back(binding);
               }
               break;
            case UndoCommand::Kind::Connect:
            case UndoCommand::Kind::Disconnect:
            {
               const bool add = (command.kind == UndoCommand::Kind::Connect) == forward;
               const UndoConnection& c = command.connection;
               if (add)
                  connectLocked(c.sourceModuleId, c.sourcePortIndex, c.destModuleId, c.destPortIndex);
               else
                  removeConnection(c);
               break;
            }
            case UndoCommand::Kind::SetControl:
               if (Module* module = getModule(command.moduleId))
                  module->setControlValue(command.control, forward ? command.after : command.before);
               break;
            case UndoCommand::Kind::MoveModule:
               if (Module* module = getModule(command.moduleId))
                  module->setPosition(forward ? command.toX : command.fromX, forward ? command.toY : command.fromY);
               break;
            case UndoCommand::Kind::Group:
               if (forward)
               {
                  for (const auto& child : command.children)
                     applyCommandLocked(child, true);
               }
               else
               {
                  for (auto it = command.children.rbegin(); it != command.children.rend(); ++it)
                     applyCommandLocked(*it, false);
               }
               break;
         }
      }

      bool ModuleCanvas::undo()
      {
         Lock lock(mMutex);
         if (!mUndoStack.canUndo())
            return false;
         cancelPointerInteraction();
         UndoSuppressor suppress(mUndoSuppressDepth);
         UndoCommand command = mUndoStack.popUndo();
         applyCommandLocked(command, false);
         mUndoStack.pushRedo(std::move(command));
         publishAudioGraphSnapshotLocked();
         return true;
      }

      bool ModuleCanvas::redo()
      {
         Lock lock(mMutex);
         if (!mUndoStack.canRedo())
            return false;
         cancelPointerInteraction();
         UndoSuppressor suppress(mUndoSuppressDepth);
         UndoCommand command = mUndoStack.popRedo();
         applyCommandLocked(command, true);
         mUndoStack.pushUndoKeepRedo(std::move(command));
         publishAudioGraphSnapshotLocked();
         return true;
      }

      int ModuleCanvas::undoDepth() const
      {
         Lock lock(mMutex);
         return static_cast<int>(mUndoStack.undoDepth());
      }

      int ModuleCanvas::redoDepth() const
      {
         Lock lock(mMutex);
         return static_cast<int>(mUndoStack.redoDepth());
      }

      void ModuleCanvas::clearUndoHistory()
      {
         Lock lock(mMutex);
         mUndoStack.clear();
      }

      void ModuleCanvas::commitGestureLocked()
      {
         if (mControlModuleId >= 0)
         {
            if (Module* module = getModule(mControlModuleId))
            {
               UndoCommand group;
               group.kind = UndoCommand::Kind::Group;
               for (const auto& [name, after] : moduleControlMap(*module))
               {
                  auto before = mGestureControls.find(name);
                  if (before != mGestureControls.end() && before->second != after)
                     group.children.push_back(UndoCommand::setControl(mControlModuleId, name, before->second, after));
               }
               if (group.children.size() == 1)
                  recordLocked(std::move(group.children.front()));
               else if (!group.children.empty())
                  recordLocked(std::move(group));
            }
         }
         if (mDraggedModuleId >= 0)
         {
            if (Module* module = getModule(mDraggedModuleId))
            {
               if (module->getX() != mGestureStartX || module->getY() != mGestureStartY)
                  recordLocked(UndoCommand::move(mDraggedModuleId, mGestureStartX, mGestureStartY,
                                                 module->getX(), module->getY()));
            }
         }
         mGestureControls.clear();
      }

   } // namespace wasm
} // namespace bespoke
