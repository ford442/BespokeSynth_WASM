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
         publishAudioGraphSnapshotLocked();
         printf("ModuleCanvas: Created with Transport and Scale modules\n");
      }

      int ModuleCanvas::createModule(const std::string& type, float x, float y)
      {
         Lock lock(mMutex);
         auto module = ModuleFactory::instance().createModule(type, mNextModuleId);
         if (!module)
         {
            printf("ModuleCanvas: Unknown module type '%s'\n", type.c_str());
            return -1;
         }

         int id = mNextModuleId++;
         module->setPosition(x, y);
         printf("ModuleCanvas: Created module '%s' (id=%d) at (%.1f, %.1f)\n",
                type.c_str(), id, x, y);
         mModules[id] = std::move(module);
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

         // Remove connections involving this module
         mConnections.erase(
         std::remove_if(mConnections.begin(), mConnections.end(),
                        [moduleId](const Connection& c)
                        {
                           return c.sourceModuleId == moduleId || c.destModuleId == moduleId;
                        }),
         mConnections.end());

         mModules.erase(moduleId);
         printf("ModuleCanvas: Deleted module %d\n", moduleId);
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

      bool ModuleCanvas::setModuleControlValue(int moduleId, const std::string& name, float value)
      {
         Lock lock(mMutex);
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return false;
         it->second->setControlValue(name, value);
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

      void ModuleCanvas::connectModules(int sourceId, int sourcePort, int destId, int destPort)
      {
         Lock lock(mMutex);
         if (!portsAreCompatible(sourceId, sourcePort, destId, destPort))
         {
            printf("ModuleCanvas: Rejected incompatible connection %d:%d -> %d:%d\n",
                   sourceId, sourcePort, destId, destPort);
            return;
         }
         for (const auto& existing : mConnections)
         {
            if (existing.sourceModuleId == sourceId && existing.sourcePortIndex == sourcePort &&
                existing.destModuleId == destId && existing.destPortIndex == destPort)
               return;
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
      }

      void ModuleCanvas::disconnectModules(int sourceId, int destId)
      {
         Lock lock(mMutex);
         mConnections.erase(
         std::remove_if(mConnections.begin(), mConnections.end(),
                        [sourceId, destId](const Connection& c)
                        {
                           return c.sourceModuleId == sourceId && c.destModuleId == destId;
                        }),
         mConnections.end());
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
         snapshot.nodes.clear();
         snapshot.connections.clear();
         snapshot.nodes.reserve(mModules.size());
         snapshot.connections.reserve(mConnections.size());

         for (const auto& [id, module] : mModules)
         {
            AudioGraphNode node;
            node.id = id;
            node.type = module->getType();
            node.enabled = module->isEnabled();

            if (const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find(node.type))
               adapter->fillAudioGraphNode(moduleControlMap(*module), node);

            snapshot.nodes.push_back(node);
         }

         for (const auto& conn : mConnections)
         {
            auto srcIt = mModules.find(conn.sourceModuleId);
            auto dstIt = mModules.find(conn.destModuleId);
            if (srcIt == mModules.end() || dstIt == mModules.end())
               continue;

            const auto& outputs = srcIt->second->getOutputs();
            const auto& inputs = dstIt->second->getInputs();
            if (conn.sourcePortIndex < 0 || conn.destPortIndex < 0 ||
                conn.sourcePortIndex >= static_cast<int>(outputs.size()) ||
                conn.destPortIndex >= static_cast<int>(inputs.size()))
               continue;

            AudioGraphConnection audioConn;
            audioConn.sourceModuleId = conn.sourceModuleId;
            audioConn.sourcePortIndex = conn.sourcePortIndex;
            audioConn.destModuleId = conn.destModuleId;
            audioConn.destPortIndex = conn.destPortIndex;
            audioConn.sourcePortType = outputs[conn.sourcePortIndex].type;
            audioConn.destPortType = inputs[conn.destPortIndex].type;
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

      void ModuleCanvas::publishAudioGraphSnapshotLocked()
      {
         AudioGraphSnapshot snapshot;
         buildAudioGraphSnapshotLocked(snapshot);
         std::atomic_store_explicit(
            &mPublishedAudioGraph,
            std::make_shared<const AudioGraphSnapshot>(std::move(snapshot)),
            std::memory_order_release);
      }

      bool ModuleCanvas::portsAreCompatible(int sourceId, int sourcePort, int destId, int destPort) const
      {
         auto sourceIt = mModules.find(sourceId);
         auto destIt = mModules.find(destId);
         if (sourceIt == mModules.end() || destIt == mModules.end() || sourceId == destId ||
             sourcePort < 0 || destPort < 0)
            return false;
         const auto& outputs = sourceIt->second->getOutputs();
         const auto& inputs = destIt->second->getInputs();
         return sourcePort < static_cast<int>(outputs.size()) && destPort < static_cast<int>(inputs.size()) &&
                outputs[sourcePort].type == inputs[destPort].type;
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
         return snapshot;
      }

      ModuleCanvas::StateSnapshot ModuleCanvas::createStateSnapshot() const
      {
         Lock lock(mMutex);

         StateSnapshot snapshot;
         snapshot.transportBPM = mTransport ? mTransport->getBPM() : 120.0f;
         snapshot.transportPlaying = mTransport && mTransport->isPlaying();
         snapshot.offsetX = mOffsetX;
         snapshot.offsetY = mOffsetY;
         snapshot.scale = mScale;
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
         std::unordered_map<int, int> idMap;

         clearUserModules();
         setViewTransform(snapshot.offsetX, snapshot.offsetY, snapshot.scale);

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
         }

         for (const auto& stateConn : snapshot.connections)
         {
            auto srcIt = idMap.find(stateConn.sourceModuleId);
            auto dstIt = idMap.find(stateConn.destModuleId);
            if (srcIt == idMap.end() || dstIt == idMap.end())
               continue;
            connectModules(srcIt->second, stateConn.sourcePortIndex, dstIt->second, stateConn.destPortIndex);
         }

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

      void ModuleCanvas::setOutputLevel(float level)
      {
         Lock lock(mMutex);
         for (auto& [id, module] : mModules)
         {
            if (module->getType() == "output")
            {
               module->setControlValue("level", level);
               break;
            }
         }
      }

      void ModuleCanvas::clearUserModules()
      {
         Lock lock(mMutex);
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

   } // namespace wasm
} // namespace bespoke
