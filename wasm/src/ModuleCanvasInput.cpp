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
      void ModuleCanvas::onMouseDown(float x, float y, int button)
      {
         Lock lock(mMutex);
         mLastMouseX = x;
         mLastMouseY = y;

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
            constexpr float kMenuWidth = 260.0f;
            constexpr float kSearchHeight = 29.0f;
            constexpr float kCategoryHeight = 25.0f;
            constexpr float kRowHeight = 24.0f;
            const auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
            const int rows = std::min(static_cast<int>(types.size()), 8);
            const float menuHeight = kSearchHeight + kCategoryHeight + 8.0f + rows * kRowHeight + 10.0f;
            const float menuX = mSpawnMenuRenderX;
            const float menuY = mSpawnMenuRenderY;

            if (x >= menuX && x <= menuX + kMenuWidth && y >= menuY && y <= menuY + menuHeight)
            {
               const int categoryValues[] = { -1, static_cast<int>(ModuleCategory::Synth), static_cast<int>(ModuleCategory::AudioEffect),
                                              static_cast<int>(ModuleCategory::Modulator), static_cast<int>(ModuleCategory::Other) };
               const float categoryY = menuY + kSearchHeight + 3.0f;
               if (y >= categoryY && y <= categoryY + 18.0f)
               {
                  const int chip = static_cast<int>((x - menuX - 8.0f) / 48.5f);
                  if (chip >= 0 && chip < 5)
                  {
                     mSpawnMenuCategory = categoryValues[chip];
                     mSpawnMenuSelectedIndex = 0;
                  }
                  return;
               }
               const float rowsY = menuY + kSearchHeight + kCategoryHeight + 2.0f;
               const int row = static_cast<int>((y - rowsY) / kRowHeight);
               if (row >= 0 && row < static_cast<int>(types.size()))
               {
                  const float worldX = screenToWorldX(mSpawnMenuX);
                  const float worldY = screenToWorldY(mSpawnMenuY - kTitleBarHeight);
                  createModule(types[row].type, worldX, worldY);
                  closeSpawnMenu();
                  return;
               }
               return;
            }
            closeSpawnMenu();
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
               mTransport->setPlaying(!mTransport->isPlaying());
               publishAudioGraphSnapshotLocked();
               return;
            }
         }

         // In-module controls (sliders, etc.) take priority over dragging
         if (button == 0)
         {
            for (auto& [id, module] : mModules)
            {
               if (module->hitTest(worldX, worldY) && module->handleMouseDown(worldX, worldY))
               {
                  mControlModuleId = id;
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
            publishAudioGraphSnapshotLocked();
         }
         mControlModuleId = -1;
         mDraggedModuleId = -1;
         mIsPanning = false;
         mIsConnecting = false;
         mConnectSourceId = -1;
         mConnectSourcePort = -1;
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
               publishAudioGraphSnapshotLocked();
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
         static const int kKeyEnter = 13;
         static const int kKeyUp = 38;
         static const int kKeyDown = 40;
         static const int kKeySlash = 191;
         static const int kKeyK = 75;

         if (!mSpawnMenuOpen && ((keyCode == kKeySlash || keyCode == '/') || ((modifiers & 4) != 0 && keyCode == kKeyK)))
         {
            openSpawnMenu(mLastMouseX, std::max(mLastMouseY, kTitleBarHeight + 4.0f));
            return;
         }

         if (mSpawnMenuOpen)
         {
            auto types = spawnMenuTypes(mSpawnMenuCategory, mSpawnMenuSearch);
            if (keyCode == kKeyEscape)
            {
               closeSpawnMenu();
               return;
            }
            if (keyCode == kKeyBackspace)
            {
               if (!mSpawnMenuSearch.empty())
                  mSpawnMenuSearch.pop_back();
               mSpawnMenuSelectedIndex = 0;
               return;
            }
            if (keyCode == kKeyUp && !types.empty())
            {
               mSpawnMenuSelectedIndex = (mSpawnMenuSelectedIndex + static_cast<int>(types.size()) - 1) % static_cast<int>(types.size());
               return;
            }
            if (keyCode == kKeyDown && !types.empty())
            {
               mSpawnMenuSelectedIndex = (mSpawnMenuSelectedIndex + 1) % static_cast<int>(types.size());
               return;
            }
            if (keyCode == kKeyEnter && !types.empty())
            {
               const int selected = std::min(mSpawnMenuSelectedIndex, static_cast<int>(types.size()) - 1);
               createModule(types[selected].type, screenToWorldX(mSpawnMenuX),
                            screenToWorldY(mSpawnMenuY - kTitleBarHeight));
               closeSpawnMenu();
               return;
            }
            if ((modifiers & (2 | 4 | 8)) == 0 && keyCode >= 32 && keyCode <= 126)
            {
               const char character = static_cast<char>(std::tolower(static_cast<unsigned char>(keyCode)));
               if (std::isalnum(static_cast<unsigned char>(character)) || character == ' ' || character == '-' || character == '_')
               {
                  mSpawnMenuSearch.push_back(character);
                  mSpawnMenuSelectedIndex = 0;
               }
            }
            return;
         }

         if (keyCode == kKeySpace && mTransport)
         {
            mTransport->setPlaying(!mTransport->isPlaying());
            publishAudioGraphSnapshotLocked();
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
