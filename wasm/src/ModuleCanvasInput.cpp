/**
 * BespokeSynth WASM
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"
#include "BespokeWasm/ModuleHelp.h"
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
      void ModuleCanvas::onMouseDown(float x, float y, int button, int modifiers)
      {
         Lock lock(mMutex);
         mLastMouseX = x;
         mLastMouseY = y;
         mHoverModuleId = -1; // a click dismisses the tooltip until the pointer moves

         // Shift-click a control arms MIDI learn; any other click disarms it.
         if (button == 0 && (modifiers & 1) != 0 && y >= kTitleBarHeight && !mSpawnMenuOpen)
         {
            if (armMidiLearnAt(x, y))
               return;
         }
         if (mMidiLearnModuleId >= 0)
            cancelMidiLearn();

         // The title-bar button is a convenient discoverable alternative to right-click.
         if (y < kTitleBarHeight)
         {
            float btnX = 140.0f;
            float btnW = 112.0f;
            float btnH = 24.0f;
            float btnTop = 8.0f;
            if (x >= btnX && x <= btnX + btnW && y >= btnTop && y <= btnTop + btnH)
            {
               if (mSpawnMenuOpen)
                  closeSpawnMenu();
               else
                  openSpawnMenu(x, kTitleBarHeight + 4.0f);
               return;
            }
            if (mSpawnMenuOpen)
               closeSpawnMenu();
            return;
         }

         if (mSpawnMenuOpen)
         {
            handleSpawnMenuClickLocked(x, y);
            return;
         }

         // Convert to world coordinates for module hit testing
         float worldX = screenToWorldX(x);
         float worldY = screenToWorldY(y - kTitleBarHeight);

         if (button == 2)
         {
            if (removeConnectionAt(x, y))
            {
               publishAudioGraphSnapshotLocked();
               return;
            }
            bool moduleAtCursor = false;
            for (const auto& [id, module] : mModules)
               moduleAtCursor = moduleAtCursor || module->hitTest(worldX, worldY);
            if (!moduleAtCursor)
            {
               openSpawnMenu(x, y);
               return;
            }
         }

         if (button == 0)
         {
            int sourceId = -1;
            int sourcePort = -1;
            if (findPortAt(worldX, worldY, true, sourceId, sourcePort))
            {
               mIsConnecting = true;
               mConnectSourceId = sourceId;
               mConnectSourcePort = sourcePort;
               mConnectEndX = x;
               mConnectEndY = y;
               mConnectTargetCompatible = true;
               return;
            }
         }

         // Transport play/stop button
         if (mTransport && mTransport->hitTest(worldX, worldY))
         {
            const float localX = worldX - mTransport->getX();
            const float localY = worldY - mTransport->getY();
            if (localX >= 10.0f && localX <= 30.0f && localY >= 15.0f && localY <= 29.0f)
            {
               toggleTransportPlaying();
               return;
            }
         }

         // In-module controls (sliders, etc.) take priority over dragging
         if (button == 0)
         {
            for (auto& [id, module] : mModules)
            {
               if (!module->hitTest(worldX, worldY))
                  continue;
               // Snapshot before handleMouseDown: clicks (step toggles) change values immediately.
               auto before = moduleControlMap(*module);
               if (module->handleMouseDown(worldX, worldY))
               {
                  mControlModuleId = id;
                  mGestureControls = std::move(before);
                  syncModuleParamsLocked(id);
                  return;
               }
            }
         }

         // Check module title bar for dragging
         for (auto& [id, module] : mModules)
         {
            if (module->hitTitleBar(worldX, worldY))
            {
               mDraggedModuleId = id;
               mGestureStartX = module->getX();
               mGestureStartY = module->getY();
               return;
            }
         }

         // Right-click for context menu, middle-click for pan
         if (button == 1 || button == 2)
         {
            mIsPanning = true;
            mPanStartX = x;
            mPanStartY = y;
            return;
         }

         // Nothing hit - start panning with left click on empty space
         mIsPanning = true;
         mPanStartX = x;
         mPanStartY = y;
      }

      void ModuleCanvas::onMouseUp(float x, float y, int button)
      {
         Lock lock(mMutex);
         if (mIsConnecting && button == 0)
         {
            const float worldX = screenToWorldX(x);
            const float worldY = screenToWorldY(y - kTitleBarHeight);
            int destId = -1;
            int destPort = -1;
            if (findPortAt(worldX, worldY, false, destId, destPort) &&
                portsAreCompatible(mConnectSourceId, mConnectSourcePort, destId, destPort))
               connectModules(mConnectSourceId, mConnectSourcePort, destId, destPort);
         }
         if (mControlModuleId >= 0)
         {
            Module* mod = getModule(mControlModuleId);
            if (mod)
               mod->handleMouseUp();
            syncModuleParamsLocked(mControlModuleId);
         }
         commitGestureLocked();
         mControlModuleId = -1;
         mDraggedModuleId = -1;
         mIsPanning = false;
         mIsConnecting = false;
         mConnectSourceId = -1;
         mConnectSourcePort = -1;
      }

      void ModuleCanvas::cancelPointerInteraction()
      {
         Lock lock(mMutex);
         if (mControlModuleId >= 0)
         {
            if (Module* mod = getModule(mControlModuleId))
               mod->handleMouseUp();
            syncModuleParamsLocked(mControlModuleId);
         }
         // Value/position changes already happened; keep them undoable.
         commitGestureLocked();
         mControlModuleId = -1;
         mDraggedModuleId = -1;
         mIsPanning = false;
         mIsConnecting = false;
         mConnectSourceId = -1;
         mConnectSourcePort = -1;
      }

      void ModuleCanvas::onLongPress(float x, float y)
      {
         Lock lock(mMutex);
         if (y < kTitleBarHeight || mSpawnMenuOpen)
            return;
         const float worldX = screenToWorldX(x);
         const float worldY = screenToWorldY(y - kTitleBarHeight);
         for (const auto& [id, module] : mModules)
         {
            if (module->hitTest(worldX, worldY))
               return;
         }
         cancelPointerInteraction();
         openSpawnMenu(x, y);
      }

      void ModuleCanvas::onMouseMove(float x, float y, float prevX, float prevY)
      {
         Lock lock(mMutex);
         mLastMouseX = x;
         mLastMouseY = y;
         if (mControlModuleId >= 0)
         {
            Module* mod = getModule(mControlModuleId);
            if (mod)
            {
               float worldX = screenToWorldX(x);
               float worldY = screenToWorldY(y - kTitleBarHeight);
               float dx = (x - prevX) / mScale;
               float dy = (y - prevY) / mScale;
               mod->handleMouseDrag(worldX, worldY, dx, dy);
               syncModuleParamsLocked(mControlModuleId);
            }
         }
         else if (mDraggedModuleId >= 0)
         {
            Module* mod = getModule(mDraggedModuleId);
            if (mod)
            {
               float dx = (x - prevX) / mScale;
               float dy = (y - prevY) / mScale;
               mod->setPosition(mod->getX() + dx, mod->getY() + dy);
            }
         }
         else if (mIsPanning)
         {
            float dx = (x - prevX) / mScale;
            float dy = (y - prevY) / mScale;
            mOffsetX += dx;
            mOffsetY += dy;
         }

         if (mIsConnecting)
         {
            mConnectEndX = x;
            mConnectEndY = y;
            const float worldX = screenToWorldX(x);
            const float worldY = screenToWorldY(y - kTitleBarHeight);
            int destId = -1;
            int destPort = -1;
            mConnectTargetCompatible = !findPortAt(worldX, worldY, false, destId, destPort) ||
                                       portsAreCompatible(mConnectSourceId, mConnectSourcePort, destId, destPort);
         }

         if (mSpawnMenuOpen)
            handleSpawnMenuHoverLocked(x, y);
         updateHoverLocked(x, y);
      }

      void ModuleCanvas::updateHoverLocked(float x, float y)
      {
         const int previousModule = mHoverModuleId;
         const std::string previousControl = mHoverControl;
         const bool previousTitleBar = mHoverTitleBar;
         updateHoverTargetLocked(x, y);
         if (mHoverModuleId != previousModule || mHoverControl != previousControl ||
             mHoverTitleBar != previousTitleBar)
            mHoverFrames = 0;
      }

      void ModuleCanvas::updateHoverTargetLocked(float x, float y)
      {
         mHoverModuleId = -1;
         mHoverControl.clear();
         mHoverTitleBar = false;
         // No tooltip while a gesture or the spawn menu owns the pointer.
         if (mSpawnMenuOpen || mControlModuleId >= 0 || mDraggedModuleId >= 0 || mIsPanning || mIsConnecting ||
             y < kTitleBarHeight)
            return;
         const float worldX = screenToWorldX(x);
         const float worldY = screenToWorldY(y - kTitleBarHeight);
         for (auto& [id, module] : mModules)
         {
            if (!module->hitTest(worldX, worldY))
               continue;
            mHoverModuleId = id;
            mHoverTitleBar = worldY < module->getY() + Module::kTitleBarHeight;
            if (!mHoverTitleBar)
               mHoverControl = module->controlNameAt(worldX, worldY);
            return;
         }
      }

      std::string ModuleCanvas::hoverHelpText() const
      {
         Lock lock(mMutex);
         const auto it = mModules.find(mHoverModuleId);
         if (it == mModules.end())
            return {};
         const Module& module = *it->second;
         const ModuleHelp& help = ModuleHelp::instance();
         if (!mHoverControl.empty())
            return help.controlTooltip(module.serializedType(), mHoverControl);
         return mHoverTitleBar ? help.description(module.serializedType()) : std::string();
      }

      void ModuleCanvas::onMouseWheel(float deltaX, float deltaY, float mouseX, float mouseY)
      {
         Lock lock(mMutex);
         (void)deltaX;
         float zoomFactor = 1.0f - deltaY * 0.001f;
         zoom(zoomFactor, mouseX, mouseY);
      }

      void ModuleCanvas::onKeyDown(int keyCode, int modifiers)
      {
         Lock lock(mMutex);
         static const int kKeyDelete = 46;
         static const int kKeyBackspace = 8;
         static const int kKeySpace = 32;
         static const int kKeyEscape = 27;
         static const int kKeySlash = 191;
         static const int kKeyK = 75;

         static const int kKeyY = 89;
         static const int kKeyZ = 90;

         if (mMidiLearnModuleId >= 0)
         {
            if (keyCode == kKeyEscape)
            {
               cancelMidiLearn();
               return;
            }
            if (keyCode == kKeyDelete || keyCode == kKeyBackspace)
            {
               // Forget the armed control's CC binding.
               removeMidiCcBinding(mMidiLearnModuleId, mMidiLearnControl);
               cancelMidiLearn();
               return;
            }
         }

         // Ctrl/Cmd+Z undo, Ctrl/Cmd+Shift+Z or Ctrl+Y redo.
         if (!mSpawnMenuOpen && (modifiers & (4 | 8)) != 0 && (keyCode == kKeyZ || keyCode == kKeyY))
         {
            if (keyCode == kKeyY || (modifiers & 1) != 0)
               redo();
            else
               undo();
            return;
         }

         if (!mSpawnMenuOpen && ((keyCode == kKeySlash || keyCode == '/') || ((modifiers & 4) != 0 && keyCode == kKeyK)))
         {
            openSpawnMenu(mLastMouseX, std::max(mLastMouseY, kTitleBarHeight + 4.0f));
            return;
         }

         if (mSpawnMenuOpen)
         {
            handleSpawnMenuKeyLocked(keyCode, modifiers);
            return;
         }

         if (keyCode == kKeySpace && mTransport)
         {
            toggleTransportPlaying();
            return;
         }

         // Delete key removes selected module
         if (keyCode == kKeyDelete || keyCode == kKeyBackspace)
         {
            if (mDraggedModuleId >= 0)
            {
               deleteModule(mDraggedModuleId);
               mDraggedModuleId = -1;
            }
         }
      }

   } // namespace wasm
} // namespace bespoke
