/**
 * BespokeSynth WASM - Module adapter boundary (ADR: docs/wasm-module-porting.md)
 *
 * Adapters connect canvas UI modules, serialization, and audio graph processing.
 * Each adapter owns a POD parameter block, runtime state, and port layout.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/DspSimd.h"
#include "BespokeWasm/Module.h"
#include "BespokeWasm/ModuleTypes.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class Module;

      using WasmControlMap = std::map<std::string, float>;
      using WasmStringMap = std::map<std::string, std::string>;

      struct WasmControlDescriptor
      {
         std::string name;
         float defaultValue = 0.0f;
      };

      struct PortDescriptor
      {
         PortType type = PortType::Audio;
         const char* name = "";
      };

      struct WasmNoteEvent
      {
         int pitch = 60;
         float velocity = 0.0f;
         bool isNoteOn = false;
         int channel = 0;
         /** Ordinal among the source's Note outputs (multi-output sources like drumseq). */
         int outputPort = 0;
      };

      struct WasmPulseEvent
      {
         double timeBeats = 0.0;
         float strength = 1.0f;
      };

      /**
       * One incoming audio cable, tagged with the destination input port index.
       * Planar channels; a mono source has numChannels == 1 and channels[1] == channels[0].
       */
      struct WasmAudioInput
      {
         const float* channels[2] = { nullptr, nullptr };
         int numChannels = 1;
         int port = 0;
      };

      struct WasmAudioProcessContext
      {
         float sampleRate = 44100.0f;
         int numSamples = 0;
         /** Channels the adapter writes (1 or 2); always 1 for modulation sources. */
         int numChannels = 1;
         /** Widest audio cable into this module (1 when it has none). */
         int inputChannels = 1;
         double blockStartTimeSeconds = 0.0;
         double beatStart = 0.0;
         double beatEnd = 0.0;
         float bpm = 120.0f;
         const float* modulationBuffer = nullptr;
         const float* inputAudio = nullptr;
         const WasmAudioInput* audioInputs = nullptr;
         int audioInputCount = 0;
         const WasmNoteEvent* notes = nullptr;
         int noteCount = 0;
         const WasmPulseEvent* pulses = nullptr;
         int pulseCount = 0;
         /** MIDI note → Hz from the Scale module (128 entries); null means 12-TET. */
         const float* noteFrequencies = nullptr;
         bool hasNoteCable = false;
         bool hasPulseCable = false;
         bool hasAudioInput = false;
      };

      inline float wasmControlValue(const WasmControlMap& controls, const char* name, float fallback)
      {
         auto it = controls.find(name);
         return it != controls.end() ? it->second : fallback;
      }

      /**
       * dst[ch] += one cable, matched to `numChannels`: a mono cable feeds both
       * channels, a stereo cable into one channel is summed to mid (L + R) / 2.
       */
      inline void accumulateAudioInput(const WasmAudioInput& input, float* const* dst, int numChannels, int numSamples)
      {
         if (numChannels == 1 && input.numChannels == 2)
         {
            dsp::accumulateScaled(dst[0], input.channels[0], numSamples, 0.5f);
            dsp::accumulateScaled(dst[0], input.channels[1], numSamples, 0.5f);
            return;
         }
         for (int ch = 0; ch < numChannels; ++ch)
            dsp::accumulateScaled(dst[ch], input.channels[ch], numSamples, 1.0f);
      }

      /** dst[ch] += every cable into input `port`. Returns false when that port has no cable. */
      inline bool accumulateAudioInputPort(const WasmAudioProcessContext& context,
                                           int port,
                                           float* const* dst,
                                           int numChannels)
      {
         bool any = false;
         for (int c = 0; c < context.audioInputCount; ++c)
         {
            const WasmAudioInput& input = context.audioInputs[c];
            if (input.port != port || !input.channels[0])
               continue;
            accumulateAudioInput(input, dst, numChannels, context.numSamples);
            any = true;
         }
         return any;
      }

      /** Widest cable into input `port`, or 0 when it has none. */
      inline int audioInputPortChannels(const WasmAudioProcessContext& context, int port)
      {
         int channels = 0;
         for (int c = 0; c < context.audioInputCount; ++c)
         {
            const WasmAudioInput& input = context.audioInputs[c];
            if (input.port == port && input.channels[0] && input.numChannels > channels)
               channels = input.numChannels;
         }
         return channels;
      }

      class WasmModuleAdapter
      {
      public:
         virtual ~WasmModuleAdapter() = default;

         virtual const char* typeId() const = 0;
         virtual const char* displayName() const = 0;
         virtual ModuleCategory category() const = 0;
         virtual bool isSingleton() const { return false; }
         virtual WasmAudioRole audioRole() const { return WasmAudioRole::None; }
         /**
          * true: the engine sums every audio cable into `channels` before processAudio.
          * false: `channels` arrive zeroed and the adapter reads context.audioInputs per port.
          */
         virtual bool sumsAudioInputs() const { return true; }
         /**
          * Channels this module writes this block (1 or 2). Called on the audio
          * thread with audioInputs/inputChannels filled; numChannels is not set
          * yet. The default follows the widest input, so mono chains stay mono
          * and a stereo source keeps its width through per-channel effects.
          */
         virtual int outputChannelCount(const void* params, const WasmAudioProcessContext& context) const
         {
            (void)params;
            return context.inputChannels;
         }
         /** NoteSource output is snapped to the graph Scale unless this returns false. */
         virtual bool quantizesNotesToScale() const { return true; }

         virtual std::vector<WasmControlDescriptor> controlDescriptors() const = 0;
         virtual std::vector<PortDescriptor> inputPorts() const { return {}; }
         virtual std::vector<PortDescriptor> outputPorts() const { return {}; }
         virtual std::unique_ptr<Module> createUiModule(int id) const = 0;

         virtual size_t paramsSize() const { return 0; }
         virtual void fillParams(const WasmControlMap& controls, void* dst) const
         {
            (void)controls;
            (void)dst;
         }

         virtual void fillParams(int moduleId,
                                 const WasmControlMap& controls,
                                 const WasmStringMap& extras,
                                 void* dst) const
         {
            (void)moduleId;
            (void)extras;
            fillParams(controls, dst);
         }

         virtual size_t runtimeStateSize() const { return 0; }
         virtual void initRuntimeState(void* runtimeState) const { (void)runtimeState; }
         virtual void destroyRuntimeState(void* runtimeState) const { (void)runtimeState; }

         /**
          * `channels` holds context.numChannels planar buffers of
          * context.numSamples each (channels[1] is null for a mono block).
          */
         virtual void processAudio(void* runtimeState,
                                   const void* params,
                                   float* const* channels,
                                   const WasmAudioProcessContext& context) const
         {
            (void)runtimeState;
            (void)params;
            (void)channels;
            (void)context;
         }

         /** One-channel processAudio (modulation sources, unit tests). */
         void processMono(void* runtimeState,
                          const void* params,
                          float* buffer,
                          const WasmAudioProcessContext& context) const
         {
            WasmAudioProcessContext mono = context;
            mono.numChannels = 1;
            float* channels[2] = { buffer, nullptr };
            processAudio(runtimeState, params, channels, mono);
         }

         virtual void emitNotesForBeatRange(void* runtimeState,
                                            const void* params,
                                            double beatStart,
                                            double beatEnd,
                                            WasmNoteEvent* outNotes,
                                            int maxNotes,
                                            int& outCount,
                                            const WasmPulseEvent* pulses = nullptr,
                                            int pulseCount = 0) const
         {
            (void)runtimeState;
            (void)params;
            (void)beatStart;
            (void)beatEnd;
            (void)outNotes;
            (void)maxNotes;
            (void)pulses;
            (void)pulseCount;
            outCount = 0;
         }

         virtual void emitPulsesForBeatRange(void* runtimeState,
                                             const void* params,
                                             double beatStart,
                                             double beatEnd,
                                             WasmPulseEvent* outPulses,
                                             int maxPulses,
                                             int& outCount,
                                             const WasmPulseEvent* inputPulses = nullptr,
                                             int inputPulseCount = 0) const
         {
            (void)runtimeState;
            (void)params;
            (void)beatStart;
            (void)beatEnd;
            (void)outPulses;
            (void)maxPulses;
            (void)inputPulses;
            (void)inputPulseCount;
            outCount = 0;
         }
      };

      class WasmModuleAdapterRegistry
      {
      public:
         static WasmModuleAdapterRegistry& instance();

         void registerAdapter(std::unique_ptr<WasmModuleAdapter> adapter);
         const WasmModuleAdapter* find(const std::string& type) const;
         const WasmModuleAdapter* adapterAt(int typeIndex) const;
         int typeIndexFor(const std::string& type) const;
         const std::vector<ModuleTypeInfo>& registeredTypes() const { return mTypes; }
         std::vector<ModuleTypeInfo> typesByCategory(ModuleCategory category) const;
         std::vector<std::string> serializableControlNames(const std::string& type) const;

         std::unique_ptr<Module> createModule(const std::string& type, int id) const;

      private:
         WasmModuleAdapterRegistry() = default;

         std::vector<std::unique_ptr<WasmModuleAdapter>> mAdapters;
         std::vector<ModuleTypeInfo> mTypes;
         std::unordered_map<std::string, int> mIndexByType;
      };

      class WasmModuleAdapterRegistrar
      {
      public:
         using Factory = std::unique_ptr<WasmModuleAdapter> (*)();

         explicit WasmModuleAdapterRegistrar(Factory factory)
         {
            if (factory)
               WasmModuleAdapterRegistry::instance().registerAdapter(factory());
         }
      };

      bool appendAudioGraphNode(AudioGraphSnapshot& snapshot,
                                int moduleId,
                                const std::string& type,
                                bool enabled,
                                const WasmControlMap& controls,
                                const WasmStringMap& extras = {});

#define BESPOKE_REGISTER_MODULE(AdapterClass)                                          \
   static bespoke::wasm::WasmModuleAdapterRegistrar g_bespoke_register_##AdapterClass( \
   []() -> std::unique_ptr<bespoke::wasm::WasmModuleAdapter>                           \
   {                                                                                   \
      return std::make_unique<AdapterClass>();                                         \
   })

   } // namespace wasm
} // namespace bespoke
