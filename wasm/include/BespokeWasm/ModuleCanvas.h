/**
 * BespokeSynth WASM - Modular Canvas System
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/AudioGraphTypes.h"
#include "BespokeWasm/MidiMap.h"
#include "BespokeWasm/ModuleTypes.h"
#include "BespokeWasm/Module.h"
#include "BespokeWasm/modules/WasmModules.h"
#include "BespokeWasm/Renderer2D.h"
#include "BespokeWasm/UndoStack.h"
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
            int schemaVersion = 4;
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

         void connectModules(int sourceId, int sourcePort, int destId, int destPort);
         void disconnectModules(int sourceId, int destId);
         const std::vector<Connection>& getConnections() const { return mConnections; }
         std::shared_ptr<const AudioGraphSnapshot> getAudioGraphSnapshot() const;
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

         void setOutputLevel(float level);

         void clearUserModules();
         void setViewTransform(float offsetX, float offsetY, float scale);
         void setupCanonicalRenderTestScene();
         /** ?renderTest=2: transport → drumseq → kick/snare/hat → mixer → compressor → output. */
         void setupDrumRenderTestScene();

         bool isSpawnMenuOpen() const { return mSpawnMenuOpen; }
         void openSpawnMenu(float x, float y);
         void closeSpawnMenu();
         int getSpawnMenuCategory() const { return mSpawnMenuCategory; }
         void setSpawnMenuCategory(int cat) { mSpawnMenuCategory = cat; }

         TransportModule* getTransport() { return mTransport; }
         ScaleModule* getScale() { return mScaleModule; }
         bool isTransportPlaying() const;
         void setTransportPlaying(bool playing);
         void toggleTransportPlaying();
         float getTransportBPM() const;
         void setTransportBPM(float bpm);

      private:
         using Lock = std::lock_guard<std::recursive_mutex>;

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
         bool connectLocked(int sourceId, int sourcePort, int destId, int destPort);
         void deleteModuleLocked(int moduleId);
         UndoModuleState captureModuleLocked(const Module& module) const;
         void restoreModuleLocked(const UndoModuleState& state);
         void recordLocked(UndoCommand command, bool coalesce = false);
         void applyCommandLocked(const UndoCommand& command, bool forward);
         void commitGestureLocked();
         void renderMidiOverlay(Renderer2D& renderer, int viewWidth, float canvasTop);

         float screenToWorldX(float screenX) const;
         float screenToWorldY(float screenY) const;
         bool findPortAt(float worldX, float worldY, bool output, int& moduleId, int& portIndex) const;
         bool portsAreCompatible(int sourceId, int sourcePort, int destId, int destPort) const;
         bool removeConnectionAt(float screenX, float screenY);
         void buildAudioGraphSnapshotLocked(AudioGraphSnapshot& snapshot) const;
         void publishAudioGraphSnapshotLocked();
         std::map<std::string, float> moduleControlMap(const Module& module) const;
         std::map<std::string, std::string> moduleStringMap(const Module& module) const;
         void seedDefaultMidiCcMapLocked();

         std::unordered_map<int, std::unique_ptr<Module>> mModules;
         std::vector<Connection> mConnections;
         int mNextModuleId = 1;
         mutable std::recursive_mutex mMutex;

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

         mutable std::shared_ptr<const AudioGraphSnapshot> mPublishedAudioGraph;

         static constexpr float kTitleBarHeight = 40.0f;
         static constexpr float kTransportBarHeight = 30.0f;

         static Color getCategoryColor(ModuleCategory cat);
      };

   } // namespace wasm
} // namespace bespoke
