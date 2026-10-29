/**
 * BespokeSynth WASM - Module adapter boundary (ADR: docs/wasm/module-porting.md)
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
#include "BespokeWasm/ResourceLifetime.h"
#include "BespokeWasm/WasmParams.h"
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

      /** Audio width a port accepts; the plan adapts each cable (PlanChannelMix), not the adapter. */
      enum class PortChannels : uint8_t
      {
         Any, // cables arrive as they are (mono or stereo)
         Mono, // stereo cables are summed to (L + R) / 2
         Stereo, // mono cables feed both sides
      };

      struct PortDescriptor
      {
         PortType type = PortType::Audio;
         const char* name = "";
         PortChannels channels = PortChannels::Any;
      };

      enum class NoteEventKind : uint8_t
      {
         Note = 0, // note on / off
         /** New bend for the sounding note at `pitch` (pitchBend; velocity and isNoteOn unused). */
         PitchBend = 1,
      };

      struct WasmNoteEvent
      {
         int pitch = 60;
         float velocity = 0.0f;
         bool isNoteOn = false;
         int channel = 0;
         /** Ordinal among the source's Note outputs (multi-output sources like drumseq). */
         int outputPort = 0;
         NoteEventKind kind = NoteEventKind::Note;
         /** Semitones: the bend a note-on starts with, or the new bend of a PitchBend event. */
         float pitchBend = 0.0f;
      };

      inline bool isPitchBendEvent(const WasmNoteEvent& event)
      {
         return event.kind == NoteEventKind::PitchBend;
      }

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

      /** Audio outputs an adapter may declare beyond its first (WasmAudioProcessContext::auxOutputs). */
      constexpr int kMaxAuxAudioOutputs = 3;

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
         /**
          * Audio outputs after the first (a send's "Send"), zeroed and numChannels wide:
          * auxOutputs[i * 2 + ch], channel 1 null for a mono block. Null when the adapter has one.
          */
         float* const* auxOutputs = nullptr;
         int auxOutputCount = 0;
         bool hasNoteCable = false;
         bool hasPulseCable = false;
         bool hasAudioInput = false;
      };

      /**
       * One block of Note input for a NoteProcessor. `notes` are the merged events of
       * the upstream Note cables (earlier emitters in the same block), or the global
       * MIDI / QWERTY performance notes when the module has no Note cable.
       */
      struct WasmNoteProcessBlock
      {
         double beatStart = 0.0;
         double beatEnd = 0.0;
         float bpm = 120.0f;
         float sampleRate = 44100.0f;
         int numSamples = 0;
         const WasmNoteEvent* notes = nullptr;
         int noteCount = 0;
         bool hasNoteCable = false;
         /** The graph Scale (capo "diatonic"); null outside the engine. */
         const GraphMusicScale* scale = nullptr;
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

         /**
          * Every control, in serialization order; ParamId is the index (WasmParams.h). Descriptors
          * with an offset are the audio parameters: fillParams writes them, and a ParamChange
          * addresses them without a snapshot rebuild. Return a static constexpr table.
          */
         virtual ParamTable paramTable() const = 0;
         /** Names and defaults from paramTable() (UI thread; allocates). */
         std::vector<WasmControlDescriptor> controlDescriptors() const;
         /** Static tables: the plan and canvas read these per cable without allocating. */
         virtual std::span<const PortDescriptor> inputPorts() const { return {}; }
         virtual std::span<const PortDescriptor> outputPorts() const { return {}; }
         virtual std::unique_ptr<Module> createUiModule(int id) const = 0;

         virtual size_t paramsSize() const { return 0; }
         /** Default-construct a Params block (AdapterStorage does this). */
         virtual void initParams(void* params) const { (void)params; }
         /**
          * UI thread, after the table's values are written: resolve resources a Params block points
          * at (a sample by hash, a looper arena or script queue by module id). May allocate.
          */
         virtual void bindParams(int moduleId, const WasmStringMap& extras, void* params) const
         {
            (void)moduleId;
            (void)extras;
            (void)params;
         }
         /**
          * Recompute fields derived from several controls after any of them changed (a sampler's
          * end kept after its start). Also runs on the audio thread after a ParamChange: must not
          * allocate or touch pointer fields.
          */
         virtual void finalizeParams(void* params) const { (void)params; }

         /**
          * Builds the Params block at `dst` (paramsSize() bytes): defaults, then every audio
          * descriptor from `controls` (its default when absent), bindParams, finalizeParams.
          */
         void fillParams(int moduleId, const WasmControlMap& controls, const WasmStringMap& extras, void* dst) const;
         void fillParams(const WasmControlMap& controls, void* dst) const { fillParams(-1, controls, {}, dst); }

         /**
          * `params` were filled by another WASM instance (the UI module) and copied into
          * this one's heap: replace any pointer they carry with this instance's own
          * object (a sample by id, a looper arena by module id). Runs when a snapshot is
          * applied in the AudioWorklet engine, never inside processAudio; may allocate.
          */
         virtual void rebindParams(int moduleId, void* params) const
         {
            (void)moduleId;
            (void)params;
         }

         /**
          * Adds the store memory `params` point at (sample ids, looper arenas by module id).
          * Publishing pins exactly what the snapshot names; see ResourceLifetime.h.
          */
         virtual void collectResources(int moduleId, const void* params, WasmResourceRefs& refs) const
         {
            (void)moduleId;
            (void)params;
            (void)refs;
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

         /**
          * NoteProcessor only: turn `block.notes` into output notes for this block.
          * Runs upstream-first, so a processor sees what the emitters cabled into it
          * produced this block. Must not allocate. Output order is delivery order.
          */
         virtual void processNotes(void* runtimeState,
                                   const void* params,
                                   const WasmNoteProcessBlock& block,
                                   WasmNoteEvent* outNotes,
                                   int maxNotes,
                                   int& outCount) const
         {
            (void)runtimeState;
            (void)params;
            (void)block;
            (void)outNotes;
            (void)maxNotes;
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
         // Lookups take a string_view: bridge callers pass C strings without building a std::string.
         [[nodiscard]] const WasmModuleAdapter* find(std::string_view type) const;
         [[nodiscard]] const WasmModuleAdapter* adapterAt(int typeIndex) const;
         [[nodiscard]] int typeIndexFor(std::string_view type) const;
         const std::vector<ModuleTypeInfo>& registeredTypes() const { return mTypes; }
         std::vector<ModuleTypeInfo> typesByCategory(ModuleCategory category) const;
         std::vector<std::string> serializableControlNames(std::string_view type) const;

         std::unique_ptr<Module> createModule(std::string_view type, int id) const;

      private:
         WasmModuleAdapterRegistry() = default;

         std::vector<std::unique_ptr<WasmModuleAdapter>> mAdapters;
         std::vector<ModuleTypeInfo> mTypes;
         /** Keys view the adapters' static typeId() literals. */
         std::unordered_map<std::string_view, int> mIndexByType;
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
