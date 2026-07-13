#pragma once

namespace bespoke::wasm
{
   void routeMouseMove(int x, int y);
   void routeMouseDown(int x, int y, int button);
   void routeMouseUp(int x, int y, int button);
   void routeMouseWheel(float deltaX, float deltaY);
   void routeKeyDown(int keyCode, int modifiers);
   void routeKeyUp(int keyCode, int modifiers);
}
