#include "BespokeWasm/WasmInputRouter.h"

#include "BespokeWasm/WasmRuntimeState.h"

#include <cstdio>

namespace bespoke::wasm
{
   namespace
   {
      constexpr int kKeyShift = 16;
      constexpr int kKeySpace = 32;
      constexpr int kKeyTab = 9;
      constexpr float kKnobSize = 80.0f;
      constexpr float kKnobStartX = 100.0f;
      constexpr float kKnobStartY = 130.0f;
      constexpr float kKnobSpacing = 120.0f;

      int mouseX = 0;
      int mouseY = 0;
      bool mouseDown = false;
      int activePointer = -1;

      const char* panelName(int index)
      {
         static const char* names[] = { "Mixer", "Effects", "Sequencer" };
         return index >= 0 && index < kPanelCount ? names[index] : "Unknown";
      }

      void selectPanel(WasmRuntimeState& state, int index)
      {
         if (index < 0 || index >= kPanelCount)
            return;
         const int previous = state.currentPanel;
         state.currentPanel = index;
         std::printf("DEBUG [Panel Switch] From:%s To:%s\n", panelName(previous), panelName(index));
      }

      void routeMouseDownWithModifiers(int x, int y, int button, int modifiers)
      {
         auto& state = runtimeState();
         mouseDown = true;
         mouseX = x;
         mouseY = y;
         if (state.viewMode == 0 && state.canvas)
         {
            state.canvas->onMouseDown(static_cast<float>(x), static_cast<float>(y), button, modifiers);
            return;
         }
         if (y >= 70 && y <= 105)
            for (int index = 0; index < kPanelCount; ++index)
               if (x >= 20 + index * 155 && x <= 170 + index * 155)
               {
                  selectPanel(state, index);
                  return;
               }
         for (size_t index = 0; index < state.knobs.size(); ++index)
         {
            const float knobX = kKnobStartX + static_cast<float>(index) * kKnobSpacing;
            if (state.knobs[index]->hitTest(static_cast<float>(x), static_cast<float>(y), knobX, kKnobStartY, kKnobSize))
            {
               state.knobs[index]->onMouseDown(static_cast<float>(x), static_cast<float>(y), knobX, kKnobStartY, kKnobSize);
               break;
            }
         }
      }
   }

   void routeMouseMove(int x, int y)
   {
      auto& state = runtimeState();
      const int previousX = mouseX;
      const int previousY = mouseY;
      mouseX = x;
      mouseY = y;
      if (state.viewMode == 0 && state.canvas)
      {
         state.canvas->onMouseMove(static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(previousX), static_cast<float>(previousY));
         return;
      }
      if (mouseDown)
         for (auto& knob : state.knobs)
            knob->onMouseDrag(static_cast<float>(x), static_cast<float>(y),
                              static_cast<float>(previousX), static_cast<float>(previousY));
   }

   void routeMouseDown(int x, int y, int button)
   {
      routeMouseDownWithModifiers(x, y, button, 0);
   }

   void routePointerDown(int pointerId, int x, int y, int button, int modifiers)
   {
      if (activePointer >= 0 && activePointer != pointerId)
         return;
      activePointer = pointerId;
      routeMouseDownWithModifiers(x, y, button, modifiers);
   }

   void routePointerMove(int pointerId, int x, int y)
   {
      // Hover (no pointer down) still updates the cursor position for "/" spawn.
      if (activePointer >= 0 && activePointer != pointerId)
         return;
      routeMouseMove(x, y);
   }

   void routePointerUp(int pointerId, int x, int y, int button)
   {
      if (activePointer != pointerId)
         return;
      activePointer = -1;
      routeMouseUp(x, y, button);
   }

   void routePointerCancel(int pointerId)
   {
      if (activePointer != pointerId)
         return;
      activePointer = -1;
      mouseDown = false;
      auto& state = runtimeState();
      if (state.canvas)
         state.canvas->cancelPointerInteraction();
      for (auto& knob : state.knobs)
         knob->onMouseUp();
   }

   int activePointerId()
   {
      return activePointer;
   }

   void routeMouseUp(int x, int y, int button)
   {
      auto& state = runtimeState();
      mouseDown = false;
      if (state.viewMode == 0 && state.canvas)
      {
         state.canvas->onMouseUp(static_cast<float>(x), static_cast<float>(y), button);
         return;
      }
      for (auto& knob : state.knobs)
         knob->onMouseUp();
   }

   void routeMouseWheel(float deltaX, float deltaY)
   {
      auto& state = runtimeState();
      if (state.viewMode == 0 && state.canvas)
      {
         state.canvas->onMouseWheel(deltaX, deltaY, static_cast<float>(mouseX), static_cast<float>(mouseY));
         return;
      }
      for (size_t index = 0; index < state.knobs.size(); ++index)
      {
         const float knobX = kKnobStartX + static_cast<float>(index) * kKnobSpacing;
         if (state.knobs[index]->hitTest(static_cast<float>(mouseX), static_cast<float>(mouseY), knobX, kKnobStartY, kKnobSize))
         {
            state.knobs[index]->onScroll(deltaY);
            break;
         }
      }
   }

   void routeKeyDown(int keyCode, int modifiers)
   {
      auto& state = runtimeState();
      if (keyCode == kKeyTab && state.legacyPanelsEnabled)
      {
         state.viewMode = state.viewMode == 0 ? 1 : 0;
         std::printf("BespokeSynth WASM: Switched to %s view\n", state.viewMode == 0 ? "Modular Canvas" : "Demo Panels");
         return;
      }
      if (state.viewMode == 0 && state.canvas)
         state.canvas->onKeyDown(keyCode, modifiers);
      if (keyCode == kKeyShift)
         for (auto& knob : state.knobs)
            knob->setFineMode(true);
      if (keyCode == kKeySpace && state.audioBackend && state.canvas)
      {
         if (state.viewMode != 0)
            state.canvas->toggleTransportPlaying();
         if (state.canvas->isTransportPlaying())
            state.audioBackend->start();
         else
            state.audioBackend->stop();
      }
   }

   void routeKeyUp(int keyCode, int modifiers)
   {
      (void)modifiers;
      if (keyCode != kKeyShift)
         return;
      for (auto& knob : runtimeState().knobs)
         knob->setFineMode(false);
   }
}
