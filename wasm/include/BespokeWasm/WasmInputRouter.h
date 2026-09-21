#pragma once

namespace bespoke::wasm
{
   void routeMouseMove(int x, int y);
   void routeMouseDown(int x, int y, int button);
   void routeMouseUp(int x, int y, int button);
   void routeMouseWheel(float deltaX, float deltaY);
   void routeKeyDown(int keyCode, int modifiers);
   void routeKeyUp(int keyCode, int modifiers);

   /**
    * Pointer events (mouse, touch, pen). The canvas is single-gesture: the first
    * pointer to go down owns the interaction until it lifts or is cancelled;
    * other pointers are ignored here (the TS shell turns two touches into pinch).
    */
   void routePointerDown(int pointerId, int x, int y, int button, int modifiers);
   void routePointerMove(int pointerId, int x, int y);
   void routePointerUp(int pointerId, int x, int y, int button);
   void routePointerCancel(int pointerId);
   int activePointerId();
}
