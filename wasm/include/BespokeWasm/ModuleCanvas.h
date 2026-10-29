/**
 * BespokeSynth WASM - Modular Canvas System
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/AudioRtGuard.h"
#include "BespokeWasm/MidiMap.h"
#include "BespokeWasm/ModuleTypes.h"
#include "BespokeWasm/Module.h"
#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/Renderer2D.h"
#include "BespokeWasm/UndoStack.h"
#include "BespokeWasm/WasmParams.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bespoke
{
   namespace wasm
   {

      class ModuleCanvas
      {
      public:
         using AudioGraphNode = bespoke::wasm::AudioGraphNode;
         using AudioGraphConnection = bespoke::wasm::AudioGraphConnection;
         using AudioGraphSnapshot = bespoke::wasm::AudioGraphSnapshot;

         struct StateModule
         {
            int id = -1;
            std::string type;
            float x = 0.0f;
            float y = 0.0f;
            bool minimized = false;
            bool enabled = true;
            std::map<std::string, float> controls;
            std::map<std::string, std::string> extras;
         };

         struct StateConnection
         {
            int sourceModuleId = -1;
            int sourcePortIndex = 0;
            int destModuleId = -1;
            int destPortIndex = 0;
         };

         struct StateSnapshot
         {
            int schemaVersion = 5; // kPatchSchemaVersion (WasmPatchState.h)
            float transportBPM = 120.0f;
            bool transportPlaying = false;
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            float scale = 1.0f;
            std::vector<StateModule> modules;
            std::vector<StateConnection> connections;
            std::vector<MidiCcBinding> midiCcMap;
            int midiNoteChannel = -1;
         };

         ModuleCanvas();
         ~ModuleCanvas() = default;

         int createModule(const std::string& type, float x, float y);
         void deleteModule(int moduleId);
         Module* getModule(int moduleId);
         int findFirstModuleOfType(const std::string& type) const;
         int getModuleCount() const { return static_cast<int>(mModules.size()); }
         /** recordUndo=false for MIDI CC / script automation (not user edits). */
         bool setModuleControlValue(int moduleId, const std::string& name, float value, bool recordUndo = true);
         bool getModuleControlValue(int moduleId, const std::string& name, float& value) const;
         bool setModuleStringProperty(int moduleId, const std::string& name, const std::string& value);
         bool getModuleStringProperty(int moduleId, const std::string& name, std::string& value) const;
         bool applyMidiCc(int channel, int cc, float value);
         int midiNoteChannel() const;
         void setMidiNoteChannel(int channel);

         /**
          * MIDI learn: the next CC (any channel) binds to this control. Arming
          * with an empty name picks the module's first serialized control.
          */
         bool armMidiLearn(int moduleId, const std::string& controlName);
         /** Shift-click entry point: arms the control under a screen point. */
         bool armMidiLearnAt(float screenX, float screenY);
         void cancelMidiLearn();
         bool isMidiLearnArmed() const;
         int midiLearnModuleId() const;
         std::string midiLearnControl() const;
         bool removeMidiCcBinding(int moduleId, const std::string& controlName);
         std::vector<MidiCcBinding> midiCcMap() const;
         /** UI range of a control (module override, else a name-based fallback, else 0..1). */
         void controlRangeFor(int moduleId, const std::string& name, float& minValue, float& maxValue) const;

         // Undo / redo: structured commands (UndoStack.h), 50 steps.
         bool undo();
         bool redo();
         int undoDepth() const;
         int redoDepth() const;
         void clearUndoHistory();

         /** Placeholders on the canvas (patch modules of a type this build lacks). */
         int placeholderCount() const;

         /** Script modules are hidden from the spawn menu unless ?script=1 enabled them. */
         void setScriptingEnabled(bool enabled);
         bool isScriptingEnabled() const;
         bool hasOutgoingConnection(int moduleId) const;

         /**
          * Called on the UI thread after every snapshot publish, before audio can
          * observe it. The runtime uses it to pre-size engine state so structural
          * edits (spawn, undo) never allocate inside the audio callback.
          */
         using SnapshotListener = std::function<void(const AudioGraphSnapshot&)>;
         void setSnapshotListener(SnapshotListener listener);

         /**
          * Control changes that need no topology publish (knob drags, MIDI CC, undo of a control,
          * transport play / tempo) go to the sink as ParamChange { moduleId, paramId, value, seq }:
          * the runtime queues them on the local engine or the worklet's event ring. A false return
          * (queue full) falls back to a full publish, as does every change while no sink is set.
          */
         using ParamSink = std::function<bool(const ParamChange&)>;
         void setParamSink(ParamSink sink);

         struct Stats
         {
            uint64_t publishCount = 0; // topology snapshots built and compiled
            uint64_t paramChangesSent = 0; // control changes that went to the sink instead
            uint64_t generation = 0;
            uint32_t paramSeq = 0;
         };
         Stats stats() const;

         /** User-facing messages from the canvas; the host shows them as a toast. */
         using NoticeListener = std::function<void(const std::string&)>;
         void setNoticeListener(NoticeListener listener);

         /**
          * Adds the cable unless it is incompatible or a duplicate. A port takes any number of
          * cables (the process plan sizes its input lists). Returns whether the cable was added.
          */
         bool connectModules(int sourceId, int sourcePort, int destId, int destPort);
         void disconnectModules(int sourceId, int destId);
         const std::vector<Connection>& getConnections() const { return mConnections; }
         /**
          * The last topology publish. Its params predate later fast-path control changes; use
          * liveAudioGraphSnapshot() for a copy that includes them.
          */
         std::shared_ptr<const AudioGraphSnapshot> getAudioGraphSnapshot() const;
         /** The published snapshot with every control change since (what the worklet engine is sent). */
         AudioGraphSnapshot liveAudioGraphSnapshot() const;
         /** Rebuilds and publishes the snapshot (a new generation) without any edit. */
         void republishAudioGraph();
         /** A fresh, untracked snapshot (generation 0) of the canvas as it is now. */
         AudioGraphSnapshot createAudioGraphSnapshot() const;
         StateSnapshot createStateSnapshot() const;
         bool applyStateSnapshot(const StateSnapshot& snapshot);

         float getOffsetX() const { return mOffsetX; }
         float getOffsetY() const { return mOffsetY; }
         float getScale() const { return mScale; }
         void setOffset(float x, float y)
         {
            mOffsetX = x;
            mOffsetY = y;
         }
         void pan(float dx, float dy)
         {
            mOffsetX += dx;
            mOffsetY += dy;
         }
         void zoom(float factor, float centerX, float centerY);

         void render(Renderer2D& renderer, int viewWidth, int viewHeight);
         void renderTitleBar(Renderer2D& renderer, int viewWidth);
         void renderSpawnMenu(Renderer2D& renderer, int viewWidth, int viewHeight);
         void renderHoverHelp(Renderer2D& renderer, int viewWidth, int viewHeight);

         void onMouseDown(float x, float y, int button) { onMouseDown(x, y, button, 0); }
         /** modifiers: 1 shift, 2 alt, 4 ctrl, 8 meta (same bits as key events). */
         void onMouseDown(float x, float y, int button, int modifiers);
         void onMouseUp(float x, float y, int button);
         /** Touch long-press: opens the spawn menu on empty canvas. */
         void onLongPress(float x, float y);
         /** Abandon the active drag/cable/pan without committing (e.g. a second finger landed). */
         void cancelPointerInteraction();
         void onMouseMove(float x, float y, float prevX, float prevY);
         void onMouseWheel(float deltaX, float deltaY, float mouseX, float mouseY);
         void onKeyDown(int keyCode, int modifiers);

         /** Per-channel pre-limit peaks for the Output VU (UI thread, once per frame). */
         void setOutputMeter(float peakLeft, float peakRight);

         void clearUserModules();
         void setViewTransform(float offsetX, float offsetY, float scale);
         // Render-test scenes (src/test_scenes, built with BESPOKE_WASM_RENDER_TESTS).
         /** ?renderTest=1: stepsequencer → oscillator → filter → gain → output, LFO on the cutoff. */
         void setupCanonicalRenderTestScene();
         /** ?renderTest=2: transport → drumseq → kick/snare/hat → mixer → compressor → output. */
         void setupDrumRenderTestScene();

         bool isSpawnMenuOpen() const { return mSpawnMenuOpen; }
         void openSpawnMenu(float x, float y);
         void closeSpawnMenu();
         int getSpawnMenuCategory() const { return mSpawnMenuCategory; }
         void setSpawnMenuCategory(int cat) { mSpawnMenuCategory = cat; }
         /** Help line under the spawn menu's selected row ("" when closed or undocumented). */
         std::string spawnMenuDescription() const;
         /** Tooltip for what the pointer rests on: a control's help, or the module's over its title bar. */
         std::string hoverHelpText() const;

         TransportModule* getTransport() { return mTransport; }
         ScaleModule* getScale() { return mScaleModule; }
         bool isTransportPlaying() const;
         void setTransportPlaying(bool playing);
         void toggleTransportPlaying();
         float getTransportBPM() const;
         void setTransportBPM(float bpm);

      private:
         using Lock = std::lock_guard<RtRecursiveMutex>;

         /** Scoped: edits inside (patch load, scenes, undo itself) are not recorded. */
         struct UndoSuppressor
         {
            explicit UndoSuppressor(int& depth)
            : mDepth(depth)
            {
               ++mDepth;
            }
            ~UndoSuppressor() { --mDepth; }
            int& mDepth;
         };

         int createModuleWithIdLocked(const std::string& type, float x, float y, int id);
         int installModuleLocked(std::unique_ptr<Module> module, float x, float y, int id);
         /** Patch load / undo restore: a type the registry lacks becomes a PlaceholderModule. */
         int createModuleOrPlaceholderLocked(const std::string& type, float x, float y, int id);
         bool isPlaceholderLocked(int moduleId) const;
         void refreshPlaceholderBadgesLocked();
         bool connectLocked(int sourceId, int sourcePort, int destId, int destPort);
         /** connectLocked, or keep the cable dangling when an end is a placeholder. */
         void restoreCableLocked(const UndoConnection& cable);
         void deleteModuleLocked(int moduleId);
         UndoModuleState captureModuleLocked(const Module& module) const;
         void restoreModuleLocked(const UndoModuleState& state);
         void recordLocked(UndoCommand command, bool coalesce = false);
         void applyCommandLocked(const UndoCommand& command, bool forward);
         void commitGestureLocked();
         /** After undo / redo: control-only commands sync params, anything else publishes. */
         void publishUndoChangeLocked(const UndoCommand& command);
         void renderMidiOverlay(Renderer2D& renderer, int viewWidth, float canvasTop);

         float screenToWorldX(float screenX) const;
         float screenToWorldY(float screenY) const;
         bool findPortAt(float worldX, float worldY, bool output, int& moduleId, int& portIndex) const;
         bool portsAreCompatible(int sourceId, int sourcePort, int destId, int destPort) const;
         bool removeConnectionAt(float screenX, float screenY);
         void buildAudioGraphSnapshotLocked(AudioGraphSnapshot& snapshot) const;
         void publishAudioGraphSnapshotLocked();
         /**
          * Sends this module's changed audio params to the sink (diffed against the mirror of what
          * the engine holds); publishes instead when there is no sink, the module is not in the
          * published graph, its enabled state or a non-param input (Scale) changed, or a send fails.
          */
         void syncModuleParamsLocked(int moduleId);
         /** Transport play state and tempo as global ParamChanges (or a publish). */
         void syncTransportLocked();
         [[nodiscard]] bool sendParamChangeLocked(int32_t moduleId, ParamId paramId, float value);
         void handleSpawnMenuClickLocked(float x, float y);
         void handleSpawnMenuKeyLocked(int keyCode, int modifiers);
         /** Spawn-menu row under the pointer becomes the selection (its help shows). */
         void handleSpawnMenuHoverLocked(float x, float y);
         void updateHoverLocked(float x, float y);
         void updateHoverTargetLocked(float x, float y);
         std::map<std::string, float> moduleControlMap(const Module& module) const;
         std::map<std::string, std::string> moduleStringMap(const Module& module) const;
         void seedDefaultMidiCcMapLocked();

         std::unordered_map<int, std::unique_ptr<Module>> mModules;
         std::vector<Connection> mConnections;
         /** Cables that touch a placeholder: not wired into the graph, but saved back unchanged. */
         std::vector<StateConnection> mDanglingConnections;
         int mNextModuleId = 1;
         mutable RtRecursiveMutex mMutex;

         float mOffsetX = 0.0f;
         float mOffsetY = 0.0f;
         float mScale = 1.0f;

         int mDraggedModuleId = -1;
         int mControlModuleId = -1;
         bool mIsPanning = false;
         float mPanStartX = 0.0f, mPanStartY = 0.0f;
         bool mIsConnecting = false;
         int mConnectSourceId = -1;
         int mConnectSourcePort = -1;
         float mConnectEndX = 0.0f, mConnectEndY = 0.0f;
         bool mConnectTargetCompatible = true;

         bool mSpawnMenuOpen = false;
         float mSpawnMenuX = 0.0f, mSpawnMenuY = 0.0f;
         int mSpawnMenuCategory = -1;
         std::string mSpawnMenuSearch;
         int mSpawnMenuSelectedIndex = 0;
         float mLastMouseX = 0.0f, mLastMouseY = 0.0f;
         float mSpawnMenuRenderX = 0.0f, mSpawnMenuRenderY = 0.0f;
         int mHoverModuleId = -1;
         std::string mHoverControl;
         bool mHoverTitleBar = false; // shows the module description
         int mHoverFrames = 0; // frames rendered on the same target (tooltip dwell)

         TransportModule* mTransport = nullptr;
         ScaleModule* mScaleModule = nullptr;
         std::vector<MidiCcBinding> mMidiCcMap;
         int mMidiNoteChannel = -1;
         bool mMidiMapFromPatch = false;

         UndoStack mUndoStack;
         int mUndoSuppressDepth = 0;
         // Gesture capture (committed as one undo step on mouse-up).
         std::map<std::string, float> mGestureControls;
         float mGestureStartX = 0.0f, mGestureStartY = 0.0f;

         int mMidiLearnModuleId = -1;
         std::string mMidiLearnControl;
         bool mScriptingEnabled = false;
         SnapshotListener mSnapshotListener;
         NoticeListener mNoticeListener;

         mutable std::shared_ptr<const AudioGraphSnapshot> mPublishedAudioGraph;
         // Control fast path (ModuleCanvasSnapshot.cpp): what the engine holds since the last publish.
         ParamSink mParamSink;
         std::vector<uint8_t> mParamMirror;
         bool mMirrorPlaying = false;
         float mMirrorBpm = 120.0f;
         uint64_t mGeneration = 0;
         uint32_t mParamSeq = 0;
         uint64_t mPublishCount = 0;
         uint64_t mParamChangesSent = 0;

         static constexpr float kTitleBarHeight = 40.0f;
         static constexpr float kTransportBarHeight = 30.0f;

         static Color getCategoryColor(ModuleCategory cat);
      };

   } // namespace wasm
} // namespace bespoke
