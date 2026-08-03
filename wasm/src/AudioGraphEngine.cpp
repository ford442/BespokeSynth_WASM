/**
 * BespokeSynth WASM - Audio graph processor
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/adapters/FilterModuleAdapter.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <unordered_set>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         constexpr float kTwoPi = 6.28318530717958647692f;

         float clampFloat(float value, float minValue, float maxValue)
         {
            return std::max(minValue, std::min(maxValue, value));
         }

         const AudioGraphNode* findNode(const AudioGraphSnapshot& graph, int id)
         {
            for (const auto& node : graph.nodes)
            {
               if (node.id == id)
                  return &node;
            }
            return nullptr;
         }

         int findOutputModuleId(const AudioGraphSnapshot& graph)
         {
            for (const auto& node : graph.nodes)
            {
               if (node.type == "output")
                  return node.id;
            }
            return -1;
         }

         bool participatesInAudioTopology(const std::string& type)
         {
            const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find(type);
            if (!adapter)
               return false;
            switch (adapter->audioRole())
            {
               case WasmAudioRole::AudioSource:
               case WasmAudioRole::AudioProcessor:
               case WasmAudioRole::Sink:
                  return true;
               default:
                  return false;
            }
         }

         bool isModulationSourceType(const std::string& type)
         {
            const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find(type);
            return adapter && adapter->audioRole() == WasmAudioRole::ModulationSource;
         }
      }

      AudioGraphEngine::RuntimeState& AudioGraphEngine::stateFor(int moduleId)
      {
         return mStates[moduleId];
      }

      void AudioGraphEngine::queueNote(const NoteMessage& note)
      {
         std::lock_guard<std::mutex> lock(mEventMutex);
         if (mNoteQueue.size() >= 256)
            mNoteQueue.pop_front();
         mNoteQueue.push_back(note);
      }

      OscillatorType AudioGraphEngine::oscillatorTypeFor(int waveform) const
      {
         switch (waveform % 4)
         {
            case 1:
               return kOsc_Saw;
            case 2:
               return kOsc_Square;
            case 3:
               return kOsc_Tri;
            default:
               return kOsc_Sin;
         }
      }

      float AudioGraphEngine::renderOscillatorSample(RuntimeState& state,
                                                     int waveform,
                                                     float frequency,
                                                     float sampleRate)
      {
         Oscillator osc(oscillatorTypeFor(waveform));
         const float sample = osc.Value(state.phase);
         state.phase += kTwoPi * clampFloat(frequency, 0.0f, sampleRate * 0.45f) / sampleRate;
         if (state.phase >= kTwoPi)
            state.phase = std::fmod(state.phase, kTwoPi);
         return sample;
      }

      float AudioGraphEngine::renderLfoSample(RuntimeState& state,
                                              int shape,
                                              float rate,
                                              float depth,
                                              float sampleRate)
      {
         const float bipolar = renderOscillatorSample(state, shape, rate, sampleRate);
         return bipolar * clampFloat(depth, 0.0f, 1.0f);
      }

      void AudioGraphEngine::processBlock(const AudioGraphSnapshot& graph,
                                          float* const* output,
                                          int numOutputChannels,
                                          int numSamples,
                                          float sampleRate)
      {
         if (!output || numOutputChannels <= 0 || numSamples <= 0)
            return;

         const int channels = std::min(numOutputChannels, 2);
         for (int ch = 0; ch < channels; ++ch)
         {
            if (output[ch])
               std::memset(output[ch], 0, static_cast<size_t>(numSamples) * sizeof(float));
         }

         if (!graph.transportPlaying || sampleRate <= 0.0f)
         {
            if (!graph.transportPlaying)
               mBeatPosition = 0.0;
            return;
         }

         std::deque<NoteMessage> notes;
         {
            std::lock_guard<std::mutex> lock(mEventMutex);
            notes.swap(mNoteQueue);
         }

         mPulseEvents.clear();
         const double beatsPerBlock = static_cast<double>(numSamples) * graph.transportBPM /
            (static_cast<double>(sampleRate) * 60.0);
         const double nextBeatPosition = mBeatPosition + beatsPerBlock;
         for (double beat = std::floor(mBeatPosition) + 1.0; beat <= std::floor(nextBeatPosition); beat += 1.0)
            mPulseEvents.push_back({ beat, 1.0f });
         mBeatPosition = nextBeatPosition;

         std::unordered_map<int, std::vector<int>> audioInputsByDest;
         std::unordered_map<int, std::vector<int>> modulationInputsByDest;
         std::unordered_map<int, std::vector<int>> audioChildrenBySource;
         std::unordered_map<int, int> indegree;
         std::unordered_set<int> audioNodeIds;

         for (const auto& node : graph.nodes)
         {
            if (node.enabled && participatesInAudioTopology(node.type))
            {
               audioNodeIds.insert(node.id);
               indegree[node.id] = 0;
            }
         }

         for (const auto& conn : graph.connections)
         {
            if (conn.sourcePortType == PortType::Audio && conn.destPortType == PortType::Audio &&
                audioNodeIds.count(conn.sourceModuleId) && audioNodeIds.count(conn.destModuleId))
            {
               audioInputsByDest[conn.destModuleId].push_back(conn.sourceModuleId);
               audioChildrenBySource[conn.sourceModuleId].push_back(conn.destModuleId);
               ++indegree[conn.destModuleId];
            }
            else if (conn.sourcePortType == PortType::Modulation &&
                     conn.destPortType == PortType::Modulation)
            {
               modulationInputsByDest[conn.destModuleId].push_back(conn.sourceModuleId);
            }
         }

         std::queue<int> ready;
         for (const auto& [id, degree] : indegree)
         {
            if (degree == 0)
               ready.push(id);
         }

         mProcessOrder.clear();
         while (!ready.empty())
         {
            const int id = ready.front();
            ready.pop();
            mProcessOrder.push_back(id);

            for (int child : audioChildrenBySource[id])
            {
               auto it = indegree.find(child);
               if (it != indegree.end() && --it->second == 0)
                  ready.push(child);
            }
         }

         if (mProcessOrder.size() != audioNodeIds.size())
            return;

         std::unordered_map<int, std::vector<float>> audioBuffers;
         std::unordered_map<int, std::vector<ModulationValue>> modulationBuffers;

         for (const auto& node : graph.nodes)
         {
            if (!node.enabled)
               continue;

            if (isModulationSourceType(node.type))
            {
               auto& state = stateFor(node.id);
               auto& buffer = modulationBuffers[node.id];
               buffer.assign(static_cast<size_t>(numSamples), ModulationValue{});
               for (int i = 0; i < numSamples; ++i)
                  buffer[i].value = renderLfoSample(state, node.lfoShape, node.lfoRate, node.lfoDepth, sampleRate);
            }
            else if (audioNodeIds.count(node.id))
            {
               audioBuffers[node.id].assign(static_cast<size_t>(numSamples), 0.0f);
            }
         }

         static const FilterModuleAdapter kFilterAdapter;

         for (int id : mProcessOrder)
         {
            const auto* node = findNode(graph, id);
            if (!node)
               continue;

            auto& buffer = audioBuffers[id];
            const WasmModuleAdapter* adapter = WasmModuleAdapterRegistry::instance().find(node->type);

            if (adapter && adapter->audioRole() == WasmAudioRole::AudioSource)
            {
               auto& state = stateFor(node->id);
               for (const auto& note : notes)
               {
                  if (note.isNoteOn)
                  {
                     state.noteFrequency = 440.0f * std::pow(2.0f, (note.pitch - 69) / 12.0f);
                     state.noteVelocity = clampFloat(note.velocity, 0.0f, 1.0f);
                     state.noteGate = true;
                     state.hasReceivedNote = true;
                  }
                  else
                  {
                     state.noteGate = false;
                     state.hasReceivedNote = true;
                  }
               }
               const float frequency = state.hasReceivedNote ? state.noteFrequency : node->frequency;
               const float level = state.hasReceivedNote && !state.noteGate ? 0.0f :
                  (state.hasReceivedNote ? state.noteVelocity : 1.0f);
               for (int i = 0; i < numSamples; ++i)
                  buffer[i] = renderOscillatorSample(state, node->waveform, frequency, sampleRate) *
                              clampFloat(node->volume, 0.0f, 1.0f) * level;
               continue;
            }

            for (int sourceId : audioInputsByDest[id])
            {
               auto srcIt = audioBuffers.find(sourceId);
               if (srcIt == audioBuffers.end())
                  continue;
               for (int i = 0; i < numSamples; ++i)
                  buffer[i] += srcIt->second[i];
            }

            if (adapter && adapter->audioRole() == WasmAudioRole::AudioProcessor && node->type == "filter")
            {
               auto& state = stateFor(node->id);
               const auto modIt = modulationInputsByDest.find(node->id);
               WasmAudioProcessContext context;
               context.sampleRate = sampleRate;
               context.numSamples = numSamples;
               context.modulationAt = [&](int sampleIndex) -> float
               {
                  float modulation = 0.0f;
                  if (modIt != modulationInputsByDest.end())
                  {
                     for (int modSourceId : modIt->second)
                     {
                        auto modBufferIt = modulationBuffers.find(modSourceId);
                        if (modBufferIt != modulationBuffers.end())
                           modulation += modBufferIt->second[sampleIndex].value;
                     }
                  }
                  return modulation;
               };
               kFilterAdapter.processAudio(&state.filterState, *node, buffer.data(), context);
            }
            else if (adapter && adapter->audioRole() == WasmAudioRole::AudioProcessor && node->type == "gain")
            {
               const float gain = clampFloat(node->gain, 0.0f, 1.0f);
               for (int i = 0; i < numSamples; ++i)
                  buffer[i] *= gain;
            }
         }

         const int outputModuleId = findOutputModuleId(graph);
         if (outputModuleId < 0)
            return;

         auto outIt = audioBuffers.find(outputModuleId);
         if (outIt == audioBuffers.end())
            return;

         for (int ch = 0; ch < channels; ++ch)
         {
            if (!output[ch])
               continue;
            for (int i = 0; i < numSamples; ++i)
               output[ch][i] = clampFloat(outIt->second[i], -1.0f, 1.0f);
         }
         mAudioTimeSeconds += static_cast<double>(numSamples) / sampleRate;
      }

   } // namespace wasm
} // namespace bespoke
