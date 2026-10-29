/**
 * BespokeSynth WASM
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/ModuleCanvasHelpers.h"
#include "BespokeWasm/ModuleFactory.h"
#include "BespokeWasm/PlaceholderModule.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include "BespokeWasm/WasmPatchState.h"
#include "BespokeWasm/AudioProcessPlan.h"
#include "BespokeWasm/AudioAnalysis.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/PixelFont.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace bespoke
{
   namespace wasm
   {
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
         return installModuleLocked(std::move(module), x, y, id);
      }

      int ModuleCanvas::installModuleLocked(std::unique_ptr<Module> module, float x, float y, int id)
      {
         mNextModuleId = std::max(mNextModuleId, id + 1);
         module->setPosition(x, y);
         printf("ModuleCanvas: Created module '%s' (id=%d) at (%.1f, %.1f)\n",
                module->serializedType().c_str(), id, x, y);
         mModules[id] = std::move(module);
         if (!mMidiMapFromPatch)
            seedDefaultMidiCcMapLocked();
         publishAudioGraphSnapshotLocked();
         return id;
      }

      int ModuleCanvas::createModuleOrPlaceholderLocked(const std::string& type, float x, float y, int id)
      {
         if (WasmModuleAdapterRegistry::instance().find(type))
            return createModuleWithIdLocked(type, x, y, id);
         if (id < 0 || mModules.count(id) != 0)
            return -1;
         // Never alias an unknown type onto another module: keep a labelled stub (ADR).
         printf("ModuleCanvas: '%s' is not in this build; loading a placeholder\n", type.c_str());
         return installModuleLocked(std::make_unique<PlaceholderModule>(id, type), x, y, id);
      }

      bool ModuleCanvas::isPlaceholderLocked(int moduleId) const
      {
         auto it = mModules.find(moduleId);
         return it != mModules.end() && it->second->isPlaceholder();
      }

      int ModuleCanvas::placeholderCount() const
      {
         Lock lock(mMutex);
         int count = 0;
         for (const auto& [id, module] : mModules)
            count += module->isPlaceholder() ? 1 : 0;
         return count;
      }

      void ModuleCanvas::refreshPlaceholderBadgesLocked()
      {
         for (const auto& [id, module] : mModules)
         {
            if (!module->isPlaceholder())
               continue;
            int kept = 0;
            for (const auto& cable : mDanglingConnections)
               kept += (cable.sourceModuleId == id || cable.destModuleId == id) ? 1 : 0;
            static_cast<PlaceholderModule*>(module.get())->setKeptCableCount(kept);
         }
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
            for (const auto& cable : mDanglingConnections)
            {
               if (cable.sourceModuleId == moduleId || cable.destModuleId == moduleId)
                  command.connections.push_back({ cable.sourceModuleId, cable.sourcePortIndex,
                                                  cable.destModuleId, cable.destPortIndex });
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
         mDanglingConnections.erase(std::remove_if(mDanglingConnections.begin(), mDanglingConnections.end(),
                                                   [moduleId](const StateConnection& cable)
                                                   {
                                                      return cable.sourceModuleId == moduleId ||
                                                             cable.destModuleId == moduleId;
                                                   }),
                                    mDanglingConnections.end());
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
         refreshPlaceholderBadgesLocked();
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
         syncModuleParamsLocked(moduleId);
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

      void ModuleCanvas::setNoticeListener(NoticeListener listener)
      {
         Lock lock(mMutex);
         mNoticeListener = std::move(listener);
      }

      bool ModuleCanvas::connectModules(int sourceId, int sourcePort, int destId, int destPort)
      {
         Lock lock(mMutex);
         if (!connectLocked(sourceId, sourcePort, destId, destPort))
            return false;
         recordLocked(UndoCommand::cable(UndoCommand::Kind::Connect, { sourceId, sourcePort, destId, destPort }));
         return true;
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
               const float portY = module->getY() + Module::kTitleBarHeight + 10.0f + static_cast<float>(index) * 15.0f;
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
            const float y1 = (source.getY() + Module::kTitleBarHeight + 10.0f + static_cast<float>(it->sourcePortIndex) * 15.0f + mOffsetY) * mScale + canvasTop;
            const float x2 = (dest.getX() + mOffsetX) * mScale;
            const float y2 = (dest.getY() + Module::kTitleBarHeight + 10.0f + static_cast<float>(it->destPortIndex) * 15.0f + mOffsetY) * mScale + canvasTop;
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
            stateModule.type = module->serializedType();
            stateModule.x = module->getX();
            stateModule.y = module->getY();
            stateModule.minimized = module->isMinimized();
            stateModule.enabled = module->isEnabled();
            stateModule.controls = moduleControlMap(*module);
            stateModule.extras = moduleStringMap(*module);
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
         snapshot.connections.insert(snapshot.connections.end(), mDanglingConnections.begin(),
                                     mDanglingConnections.end());

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
               newId = createModuleOrPlaceholderLocked(stateModule.type, stateModule.x, stateModule.y, mNextModuleId);
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
            if (isPlaceholderLocked(srcIt->second) || isPlaceholderLocked(dstIt->second))
            {
               mDanglingConnections.push_back({ srcIt->second, stateConn.sourcePortIndex, dstIt->second,
                                                stateConn.destPortIndex });
               continue;
            }
            connectModules(srcIt->second, stateConn.sourcePortIndex, dstIt->second, stateConn.destPortIndex);
         }
         refreshPlaceholderBadgesLocked();

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

   } // namespace wasm
} // namespace bespoke
