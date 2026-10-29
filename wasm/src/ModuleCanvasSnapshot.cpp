/**
 * BespokeSynth WASM - Canvas audio graph snapshots and the control fast path
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/AudioProcessPlan.h"
#include "BespokeWasm/ResourceLifetime.h"
#include "BespokeWasm/WasmModuleAdapter.h"
#include <algorithm>

namespace bespoke
{
   namespace wasm
   {
      void ModuleCanvas::setSnapshotListener(SnapshotListener listener)
      {
         Lock lock(mMutex);
         mSnapshotListener = std::move(listener);
         if (mSnapshotListener && mPublishedAudioGraph)
            mSnapshotListener(*mPublishedAudioGraph);
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
         for (const auto& name : module.preservedControlNames())
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
         snapshot.generation = ++mGeneration;
         snapshot.paramSeq = mParamSeq;
         // Pin the samples / looper arenas this snapshot names; the host reclaims the rest.
         retainResources(collectSnapshotResources(snapshot), snapshot.generation);
         mParamMirror = snapshot.paramArena;
         mMirrorPlaying = snapshot.transportPlaying;
         mMirrorBpm = snapshot.transportBPM;
         ++mPublishCount;
         auto published = std::make_shared<const AudioGraphSnapshot>(std::move(snapshot));
         if (mSnapshotListener)
            mSnapshotListener(*published);
         std::atomic_store_explicit(&mPublishedAudioGraph, std::move(published), std::memory_order_release);
      }

      ModuleCanvas::AudioGraphSnapshot ModuleCanvas::createAudioGraphSnapshot() const
      {
         Lock lock(mMutex);
         AudioGraphSnapshot snapshot;
         buildAudioGraphSnapshotLocked(snapshot);
         compileAudioProcessPlan(snapshot, snapshot.processPlan);
         return snapshot;
      }

      void ModuleCanvas::republishAudioGraph()
      {
         Lock lock(mMutex);
         publishAudioGraphSnapshotLocked();
      }

      void ModuleCanvas::setParamSink(ParamSink sink)
      {
         Lock lock(mMutex);
         mParamSink = std::move(sink);
      }

      ModuleCanvas::Stats ModuleCanvas::stats() const
      {
         Lock lock(mMutex);
         Stats stats;
         stats.publishCount = mPublishCount;
         stats.paramChangesSent = mParamChangesSent;
         stats.generation = mGeneration;
         stats.paramSeq = mParamSeq;
         return stats;
      }

      ModuleCanvas::AudioGraphSnapshot ModuleCanvas::liveAudioGraphSnapshot() const
      {
         Lock lock(mMutex);
         AudioGraphSnapshot live = mPublishedAudioGraph ? *mPublishedAudioGraph : AudioGraphSnapshot{};
         if (live.paramArena.size() == mParamMirror.size())
            live.paramArena = mParamMirror;
         live.transportPlaying = mMirrorPlaying;
         live.transportBPM = mMirrorBpm;
         live.paramSeq = mParamSeq;
         return live;
      }

      bool ModuleCanvas::sendParamChangeLocked(int32_t moduleId, ParamId paramId, float value)
      {
         if (!mParamSink)
            return false;
         // The seq is spent even when the send fails: the publish that follows covers it.
         const ParamChange change{ moduleId, paramId, value, ++mParamSeq };
         if (!mParamSink(change))
            return false;
         ++mParamChangesSent;
         return true;
      }

      void ModuleCanvas::syncTransportLocked()
      {
         const bool playing = mTransport && mTransport->isPlaying();
         const float bpm = mTransport ? mTransport->getBPM() : 120.0f;
         if (!mParamSink || !mPublishedAudioGraph)
         {
            publishAudioGraphSnapshotLocked();
            return;
         }
         if (playing != mMirrorPlaying)
         {
            if (!sendParamChangeLocked(kGlobalParamModule, static_cast<ParamId>(GlobalParam::TransportPlaying),
                                       playing ? 1.0f : 0.0f))
            {
               publishAudioGraphSnapshotLocked();
               return;
            }
            mMirrorPlaying = playing;
         }
         if (bpm != mMirrorBpm)
         {
            if (!sendParamChangeLocked(kGlobalParamModule, static_cast<ParamId>(GlobalParam::TransportBpm), bpm))
            {
               publishAudioGraphSnapshotLocked();
               return;
            }
            mMirrorBpm = bpm;
         }
      }

      void ModuleCanvas::syncModuleParamsLocked(int moduleId)
      {
         auto it = mModules.find(moduleId);
         if (it == mModules.end())
            return;
         const Module& module = *it->second;
         if (!mParamSink || !mPublishedAudioGraph || (mScaleModule && moduleId == mScaleModule->getId()))
         {
            // Scale root / mode / tuning live outside the params block.
            publishAudioGraphSnapshotLocked();
            return;
         }
         if (mTransport && moduleId == mTransport->getId())
            syncTransportLocked();

         const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find(module.getType());
         if (!adapter)
            return; // a placeholder plays nothing
         const AudioGraphSnapshot& graph = *mPublishedAudioGraph;
         const auto& nodeIndexById = graph.processPlan.nodeIndexByModuleId;
         const int nodeIndex =
         moduleId >= 0 && static_cast<size_t>(moduleId) < nodeIndexById.size() ? nodeIndexById[static_cast<size_t>(moduleId)] : -1;
         if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= graph.nodes.size())
         {
            publishAudioGraphSnapshotLocked();
            return;
         }
         const AudioGraphNode& node = graph.nodes[static_cast<size_t>(nodeIndex)];
         if (node.id != moduleId || node.enabled != module.isEnabled() || node.paramSize != adapter->paramsSize() ||
             static_cast<size_t>(node.paramOffset) + node.paramSize > mParamMirror.size())
         {
            publishAudioGraphSnapshotLocked();
            return;
         }
         if (node.paramSize == 0)
            return;

         uint8_t* params = mParamMirror.data() + node.paramOffset;
         bool changed = false;
         for (const ParamDescriptor& param : adapter->paramTable())
         {
            if (!param.isAudio())
               continue;
            const float value = module.getControlValue(param.name);
            if (paramMatches(params, param, value))
               continue;
            if (!sendParamChangeLocked(moduleId, param.id, value))
            {
               publishAudioGraphSnapshotLocked();
               return;
            }
            writeParam(params, param, value);
            changed = true;
         }
         if (changed)
            adapter->finalizeParams(params);
      }

   } // namespace wasm
} // namespace bespoke
