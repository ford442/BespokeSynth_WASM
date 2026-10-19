# Live instrument: MIDI, keyboard, undo, tuning, touch, scripts

The browser port plays as an instrument, not only a patch editor. This page covers the input surfaces and the contracts behind them. Python/Pyodide, OSC, Ableton Link, VST, and multiplayer patches are out of scope; see [python.md](python.md).

## MIDI in, channel, and CC learn

- **Inputs:** header **MIDI** button → `requestMIDIAccess({ sysex: false })`. Every input routes through `routeMidiMessage` (`src/midi.ts`) to `_bespoke_midi_note_on/off` and `_bespoke_midi_cc`. Hot-plugged inputs attach automatically.
- **Channel:** the MIDI popover's *Note channel* (`_bespoke_set_midi_note_channel`, `-1` = omni) filters note messages. It is saved as `midi.note_channel` in patch JSON. QWERTY notes follow the filter, so they are never dropped.
- **Learn:** Shift-click a control on the canvas. An orange outline and a banner show the armed control. The next CC on any channel binds that channel and CC to the control, using the control's UI range (slider-panel spec, else the fallback table in `ModuleCanvas.cpp`). Press Esc to cancel, or Delete/Backspace while armed to unmap. A CC that is already bound moves to the new control, and each control keeps one CC. Mapped modules show `CCnn name` badges.
- Learned maps are saved in patch JSON (`midi.cc[]`: channel, cc, module id, control, min, max), so they carry into share URLs. After a learn, spawning or deleting modules no longer re-seeds the default CC map.
- C ABI: `bespoke_midi_learn_arm(moduleId, control)`, `bespoke_midi_learn_arm_at(x, y)`, `bespoke_midi_learn_cancel`, `bespoke_get_midi_cc_map_json`, `bespoke_midi_cc_unbind`.

## Computer keyboard (QWERTY piano)

`src/app/qwertyPiano.ts` uses **physical key positions** (`KeyboardEvent.code`), so it works on any layout and with Shift held:

| Keys | Action |
|---|---|
| `Z S X D C V G B H N J M , L . ;` | C4 up to D#5 (bottom row + black keys) |
| `Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P` | C5 up to E6 |
| `-` / `=` | octave down / up |
| `[` / `]` | default velocity −/+ 0.1 (starts at 0.8) |
| Shift + note | accent (velocity 1.0) |

The piano uses the same path as hardware MIDI (`bespoke_midi_note_on`), so Scale quantization and Scala tunings apply. It pauses while the spawn-menu search is open (`bespoke_is_text_entry_active`), when a DOM text field has focus, and for Ctrl/Alt/Meta chords. `/` stays the spawn shortcut. You can turn it off in the MIDI popover.

## MIDI out and clock

Pick an output in the MIDI popover. Two paths feed it, and they never overlap:

- **Performance notes** (QWERTY and keyboard-style script `noteOn`) are echoed as played by `LiveInstrument.echoPerformanceNote`.
- **Graph notes** (`stepsequencer`, `drumseq`, `script` with a cable, `arpeggiator`, `chorder`, ...) are the notes the audio engine plays. While an output is selected, `GraphNotePump` polls `bespoke_drain_graph_notes` every 8 ms and sends them with a Web MIDI timestamp that adds the audio still queued ahead of the speakers (worklet ring depth, or the device latency with the worklet engine), so the hardware sounds with the browser. With no output selected the engine records nothing. Stopping the transport sends note-offs for anything still sounding.

Dedup rules: the engine never reports performance notes, so a raw key and its graph note cannot both go out, with one exception handled in `PerformanceEcho` (`src/app/performanceEcho.ts`): when a `chorder`/`arpeggiator`/... has no Note cable it listens to the performance, and `bespoke_performance_notes_processed()` is 1. The raw key is then held back and the processor's output (reported by the engine) is what the port hears. A note-on already sent raw is always paired with its note-off, even if a processor is added while the key is down. `bespoke_script_note` returns 2 for the keyboard-style path (the host echoes) and 1 for notes queued on the script's own cables (the engine reports them).

With `?midiClock=1`, `MidiClock` also sends Start/Stop on transport changes and 24 PPQ `0xF8` clock ticks. Ticks are scheduled up to 100 ms ahead with Web MIDI timestamps. Every sent message goes to a 64-entry log (`__bespoke.getMidiOutLog()`).

## Undo / redo

`UndoStack.h` + `ModuleCanvas` record **structured commands**, not JSON snapshots:

| Command | Recorded when |
|---|---|
| `AddModule` | spawn menu, API `createModule` |
| `RemoveModule` | Delete key, API; stores the module plus its cables and CC bindings |
| `Connect` / `Disconnect` | cable drag, right-click cable delete, API |
| `SetControl` | end of a control drag (one step per gesture), API `setControlValue` (coalesced within 400 ms) |
| `MoveModule` | end of a title-bar drag (mouse or touch) |
| `Group` | one gesture that changed several controls, or a multi-cable disconnect |

- Ctrl/Cmd+Z undoes. Ctrl/Cmd+Shift+Z or Ctrl+Y redoes. The header **↶ ↷** buttons do the same. History holds 50 steps, and a new edit clears the redo branch.
- **Not recorded:** MIDI CC, script automation (`bespoke_script_set_control`), transport play/stop, patch load, render-test scenes. A patch load clears the history.
- Undo restores the **same module id**, so cables, CC maps, and script references stay valid.
- **Real-time safety:** undo mutates the canvas and republishes an `AudioGraphSnapshot`. The canvas `SnapshotListener` calls `AudioGraphEngine::prepareRuntimeStates` on the UI thread. That call creates state for new nodes and resets, in place, the state of nodes that left the graph, so `processBlock` never grows an arena. The unit test runs 20 undo/redo steps with `BESPOKE_WASM_RT_ASSERT` trapping any `new` inside the audio block. This depends on the graph running on the main thread (`BESPOKE_WASM_THREADS=OFF`).

## Scala tunings (.scl / .kbm)

- Drop a `.scl` or `.kbm` file on the canvas (the same drop target as samples), use the **Sample** button's file picker, or call `__bespoke.loadTuning('scl', text, name)`.
- `libs/tuning-library` (git submodule, same as desktop) parses the file. `wasm/include/BespokeWasm/Tunings.h` is now a compatibility wrapper around it.
- `TuningTable.cpp` is the only translation unit built with `-fwasm-exceptions`, so a malformed file is rejected with an error string instead of aborting the module. Both targets link with `-fwasm-exceptions`.
- The Scale module stores the raw text in patch extras (`scl`, `sclName`, `kbm`, `kbmName`) and parses it on the UI thread. The published snapshot carries a 128-entry MIDI-note → Hz table (`GraphMusicScale::noteFrequency`). Oscillator and sampler voices read it through `WasmAudioProcessContext::noteFrequencies`.
- While a Scala tuning is active, 12-TET mode quantization is bypassed: the `.kbm` defines which keys play which degrees. Clearing the text restores 12-TET.

## Touch and pen

The canvas uses Pointer Events (`src/app/inputBridge.ts`) with `touch-action: none`:

- One finger or pen: the same gesture as the left mouse button (drag modules, knobs, cables; pan on empty canvas).
- Two fingers: pinch-zoom around the midpoint and pan (`bespoke_canvas_zoom`, `bespoke_canvas_pan`). The first finger's gesture is cancelled, not committed as a click.
- Long-press (500 ms, under 10 px of movement) on empty canvas opens the spawn menu (`bespoke_long_press`).
- C ABI: `bespoke_pointer_down/move/up/cancel(pointerId, …)`. The first pointer down owns the gesture until it lifts. `bespoke_mouse_*` remain as the fallback for browsers without Pointer Events.

## Script module (`?script=1`)

JavaScript livecoding in a sandboxed worker. It is off unless the URL has `?script=1`. Without the flag, the `script` type is hidden from the spawn menu, though patches that contain it still load.

- **Module:** `script` (`ScriptModuleAdapter`) has no DSP. It is a `NoteSource` with one Note output, and its source text lives in `extras.script`. Notes reach the audio side through the lock-free `ScriptNoteQueue`. An uncabled script module plays like a MIDI keyboard instead.
- **Panel:** the header **Script** button opens an editor and log. Code runs only on **Run** (or Ctrl/Cmd+Enter). Scripts restored from a patch or share URL are shown but never auto-run.
- **API** (a frozen `bespoke` object):

| Call | Notes |
|---|---|
| `onBeat(fn)` | `fn(beat)` on each beat while playing; `beat % 4 === 0` is beat 1 of a bar (max 16 handlers) |
| `schedule(beatsFromNow, fn)` | one-shot, transport-relative (max 256 pending) |
| `setControl(id, name, value)` | applied on the UI thread; not recorded for undo |
| `getControl(id, name)` | returns a Promise |
| `noteOn(pitch, velocity?, durationMs?)`, `noteOff(pitch)` | also echoed to MIDI out (unless a Note processor owns the performance) |
| `findModule(type)`, `modules()`, `transport`, `scriptModuleId` | read-only views |
| `setTimeout(fn, ms)`, `clearTimeout(h)` | 5 ms – 60 s, max 64 pending |
| `log(...args)` | 500 chars per line |

- **Security** (follows [python.md](python.md#security-review)):
  - The worker is built from a Blob URL.
  - Before user code runs, `fetch`, XHR, WebSocket, WebTransport, EventSource, `importScripts`, IndexedDB, Cache, nested workers, BroadcastChannel/MessageChannel, `navigator`, `WebAssembly`, raw timers, and `postMessage` are overwritten on every object in the global prototype chain.
  - Source containing `import` is refused, both in the host and in the worker, because `import()` needs no global.
  - The worker never sees the WASM heap, Emscripten FS, or DOM.
  - The host whitelists message types, requires finite numbers, `[A-Za-z0-9_]` control names, and pitches 0–127, and drops floods (more than 400 messages per 250 ms).
  - A worker that misses its ping for 2 s is terminated and recreated, twice at most, then marked `killed`.
- Nothing from a script runs in the audio callback: controls go to the canvas (UI thread), and notes go through fixed-size queues.

## Tests

- C++ (`npm run test:unit`): `test_scala_tuning`, `test_undo_redo` (20 steps + RT allocation guard), `test_midi_learn_and_channel`, `test_script_note_queue`.
- TS (`npm run test:unit:js`): `qwertyPiano.test.ts`, `scriptSandbox.test.ts`.
- Playwright (`tests/e2e/live-instrument.spec.ts`): learn → reload; QWERTY pitch under Scale quantization; MIDI out to a stubbed loopback port; 20 undos while playing; `.scl` drop; touch drag/pinch/long-press through CDP (Chromium); script beat-1 control and blocked `fetch`; hung-script restart.
