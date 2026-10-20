/**
 * BespokeSynth WASM - Input / MIDI C ABI
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WasmBridge.h"
#include "BespokeWasm/WasmBridgeInternal.h"
#include "BespokeWasm/WasmInputRouter.h"
#include "BespokeWasm/AudioGraphEngine.h"
#include "BespokeWasm/AudioRemote.h"
#include "BespokeWasm/WasmRuntimeState.h"

#include <algorithm>
#include <emscripten.h>

using namespace bespoke::wasm;

extern "C" {

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_move(int x, int y)
{
   routeMouseMove(x, y);
}

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_down(int x, int y, int button)
{
   gHostMouseDownCallCount.fetch_add(1, std::memory_order_relaxed);
   routeMouseDown(x, y, button);
}

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_up(int x, int y, int button)
{
   routeMouseUp(x, y, button);
}

EMSCRIPTEN_KEEPALIVE void bespoke_mouse_wheel(float deltaX, float deltaY)
{
   routeMouseWheel(deltaX, deltaY);
}

EMSCRIPTEN_KEEPALIVE void bespoke_key_down(int keyCode, int modifiers)
{
   routeKeyDown(keyCode, modifiers);
}

EMSCRIPTEN_KEEPALIVE void bespoke_key_up(int keyCode, int modifiers)
{
   routeKeyUp(keyCode, modifiers);
}

/**
 * A performance note (MIDI in, QWERTY, the on-screen piano). `time` is when it should start, in
 * AudioContext seconds (0 = now): the AudioWorklet engine starts it on that frame; SDL and the
 * main-thread ring play it at their next block.
 */
EMSCRIPTEN_KEEPALIVE void bespoke_midi_note_on(int channel, int pitch, float velocity, double time)
{
   auto& state = runtimeState();
   if (!state.audioGraphEngine)
      return;
   if (state.canvas)
   {
      const int listenChannel = state.canvas->midiNoteChannel();
      if (listenChannel >= 0 && channel != listenChannel)
         return;
   }
   queuePerformanceNote({ pitch, std::max(0.0f, std::min(1.0f, velocity)), true, time > 0.0 ? time : 0.0, channel });
}

EMSCRIPTEN_KEEPALIVE void bespoke_midi_note_off(int channel, int pitch, double time)
{
   auto& state = runtimeState();
   if (!state.audioGraphEngine)
      return;
   if (state.canvas)
   {
      const int listenChannel = state.canvas->midiNoteChannel();
      if (listenChannel >= 0 && channel != listenChannel)
         return;
   }
   queuePerformanceNote({ pitch, 0.0f, false, time > 0.0 ? time : 0.0, channel });
}

EMSCRIPTEN_KEEPALIVE void bespoke_midi_cc(int channel, int cc, float value)
{
   auto& state = runtimeState();
   if (!state.canvas)
      return;
   state.canvas->applyMidiCc(channel, cc, std::max(0.0f, std::min(1.0f, value)));
}

} // extern "C"
