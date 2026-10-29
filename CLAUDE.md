# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Quick Start

### Development Environment
- **Node.js**: 22.6+ required (check via `node --version`; unit tests use `--experimental-strip-types`)
- **npm**: 9+ (check via `npm --version`)
- **Emscripten SDK**: Required for WASM builds (see [Building WASM](#building-wasm))

### Common Commands

**TypeScript/Web Development**:
```bash
npm install                 # Install dependencies (run once)
npm run build              # Full build: WASM + TypeScript + Webpack
npm run build:web-only     # Rebuild the web bundle only, no WASM (faster iteration)
npm run dev                # Start dev server at http://localhost:8080
npm run serve              # Serve dist/ on port 8000 with COOP/COEP (AudioWorklet works)
npm run lint               # Run ESLint on TypeScript
npm run type-check         # Run TypeScript type checker
npm run clean              # Remove dist/, build outputs
```

**WASM Development**:
```bash
cd wasm && ./build.sh      # Build WASM module (requires Emscripten)
cd wasm && ./codespace_build.sh  # Build in GitHub Codespaces
```

## Architecture Overview

### Two Distinct Code Paths

This project maintains **two separate implementations**:

1. **Desktop Build** (`Source/` directory)
   - Full BespokeSynth using JUCE framework
   - C++ audio processing
   - NanoVG vector graphics
   - Mostly not part of the WASM build (see below)

2. **WASM/Web Build** (`wasm/` directory)
   - Simplified port for browser
   - Emscripten toolchain
   - WebGPU rendering
   - SDL2 audio backend
   - Own modules, UI, rendering and audio graph, but **not fully independent** of `Source/`: `wasm/CMakeLists.txt` compiles 18 desktop `Source/*.cpp` DSP/utility files into the UI module (`CORE_SOURCES`: `SynthGlobals`, `OpenFrameworksPort`, `ChannelBuffer`, `RollingBuffer`, `ADSR`, `BiquadFilter`, `Oscillator`, `LFO`, `Scale`, `Transport`, `MathUtils`, `Ramp`, `PerlinNoise`, `PeakTracker`, `JumpBlender`, `Curve`, `FileStream`, `ModulationChain`), and `BiquadFilter`/`Oscillator` into the AudioWorklet engine and the C++ unit tests. `Source/` is also on the include path.

**Key Constraint**: Most of `Source/` (modules, UI, JUCE integration) does not apply to WASM; the WASM build has its own modules (`WasmModuleAdapter`s), rendering, and audio systems. The shared DSP files above do, so a change to one of them changes both builds and must stay WASM-safe (see [docs/wasm/module-porting.md](docs/wasm/module-porting.md)).

### Layered Architecture

```
┌─────────────────────────────────────────┐
│  TypeScript/JavaScript (src/)           │
│  - UI orchestration                     │
│  - WASM module initialization           │
│  - Canvas management                    │
└──────────┬────────────────────────────┬─┘
           │                            │
           ▼                            ▼
┌──────────────────────┐    ┌──────────────────────┐
│  WebGPU Renderer     │    │  SDL2 Audio Backend  │
│  (WebGPURenderer.cpp)│    │  (SDL2AudioBackend)  │
│  - Shader compilation│    │  - Sample callback   │
│  - 2D rendering      │    │  - Buffer management │
└──────────┬───────────┘    └──────────┬───────────┘
           │                            │
           └────────────┬───────────────┘
                        ▼
            ┌─────────────────────────┐
            │  WasmBridge (C API)     │
            │  - Module management    │
            │  - Input handling       │
            │  - State queries        │
            └─────────────────────────┘
```

### Key Directories

| Directory | Purpose |
|-----------|---------|
| `src/` | TypeScript entry point and web app shell |
| `wasm/src/` | WASM C++ implementation (rendering, audio, bridge) |
| `wasm/include/` | C++ headers for WASM components |
| `wasm/shaders/` | WebGPU shaders (WGSL) for UI rendering |
| `wasm/types/` | TypeScript type definitions for WASM API |
| `Source/` | Desktop build source (not used in WASM) |
| `libs/` | Dependencies (JUCE, Emscripten, etc.) |
| `dist/` | Build output directory (webpack + WASM artifacts) |
| `wasm/dist/` | WASM build artifacts (JS glue code + .wasm binary) |

## Building WASM

### Prerequisites

1. Install the pinned Emscripten SDK (see `.emscripten-version`, currently **6.0.3**):
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   EMSCRIPTEN_VERSION="$(tr -d '[:space:]' < /path/to/BespokeSynth_WASM/.emscripten-version)"
   ./emsdk install "$EMSCRIPTEN_VERSION"
   ./emsdk activate "$EMSCRIPTEN_VERSION"
   source ./emsdk_env.sh  # Required before every build
   ```

2. Verify installation:
   ```bash
   emcc --version  # Should show 6.0.3 or newer (emdawnwebgpu required)
   ```

### Building Steps

1. Activate Emscripten:
   ```bash
   source /path/to/emsdk/emsdk_env.sh
   ```

2. Run build:
   ```bash
   npm run build          # Full build including WASM
   # or
   npm run build:wasm     # WASM only
   ```

3. Output files:
   - `wasm/dist/index.html` - Entry page
   - `wasm/dist/BespokeSynthWASM.js` - JavaScript glue code
   - `wasm/dist/BespokeSynthWASM.wasm` - WebAssembly binary (~1.4 MB)
   - `wasm/dist/BespokeSynthWASM_audio.js` / `.wasm` - DSP-only engine loaded into the AudioWorklet (~0.5 MB)

### Codespaces & Quick Builds

In GitHub Codespaces or environments without persistent Emscripten:
```bash
cd wasm && ./codespace_build.sh
```

This installs Emscripten and builds in one step.

## Development Workflow

### Web Development (Fastest Iteration)

When working on TypeScript/UI only (no WASM changes):

```bash
# 1. Initial setup
npm install

# 2. Run dev server with hot reload
npm run dev
# Opens http://localhost:8080 in browser

# 3. Edit TypeScript files in src/
# Changes hot-reload automatically in browser
```

The dev server runs webpack dev server with:
- TypeScript compilation
- CSS preprocessing
- Hot module replacement
- Source maps for debugging

### WASM Development

When modifying C++ (`wasm/src/`) or shaders:

```bash
# 1. Ensure Emscripten is sourced
source /path/to/emsdk/emsdk_env.sh

# 2. Build WASM
npm run build:wasm
# OR manually:
cd wasm && ./build.sh

# 3. Rebuild web side (TypeScript remains unchanged)
npm run build:web-only

# 4. Serve and test
npm run serve
# Open http://localhost:8000 in browser
```

### Full Clean Build

When you encounter build artifacts issues:

```bash
npm run clean:all       # Remove node_modules, dist, build artifacts
npm install             # Reinstall dependencies
npm run build           # Full rebuild
npm run dev
```

## WASM Module Initialization

### Initialization Sequence

The TypeScript entry point (`src/index.ts`) loads and initializes the WASM module in stages:

1. **WASM Script Load** (`loadWasmModule`)
   - Dynamically injects `wasm/BespokeSynthWASM.js`
   - Waits for factory function or Module initialization

2. **WebGPU Setup** (in C++)
   - Create GPU instance
   - Request adapter and device
   - Compile shader pipelines

3. **Audio Initialization**
   - Configure SDL2 audio backend
   - Set sample rate (typically 44100 Hz)
   - Set buffer size (typically 512 or 1024)

4. **Control Creation**
   - Instantiate knobs, sliders, and other UI elements

### WASM API (C Functions)

Exported functions in `WasmBridge.h`:

```c
// Lifecycle
int bespoke_init(int width, int height, int sampleRate, int bufferSize);
void bespoke_process_events(void);
void bespoke_shutdown(void);

// Audio
void bespoke_process_audio(void);
void bespoke_set_sample_rate(int sampleRate);
void bespoke_set_buffer_size(int bufferSize);

// Rendering
void bespoke_render(void);
void bespoke_resize(int width, int height);

// Input
void bespoke_mouse_move(int x, int y);
void bespoke_mouse_down(int x, int y, int button);
void bespoke_mouse_up(int x, int y, int button);
void bespoke_key_down(int keyCode, int modifiers);

// Module control
int bespoke_create_module(const char* type, float x, float y);
void bespoke_delete_module(int moduleId);
void bespoke_set_control_value(int moduleId, const char* name, float value);
float bespoke_get_control_value(int moduleId, const char* name);
```

### TypeScript-C++ Binding

The TypeScript module loads WASM functions via the Emscripten Module object:

```typescript
// Access WASM functions from TypeScript
const Module = await loadWasmModule(canvas);

// Call exported C functions
const initResult = Module._bespoke_init(width, height, sampleRate, bufferSize);
Module._bespoke_render();
Module._bespoke_mouse_down(x, y, button);
```

## Key Implementation Files

### TypeScript/Web (`src/`)
- **`index.ts`** - Main entry point, WASM loader, BespokeSynthApp class
- **`index.html`** - HTML template (minimal, mostly generated by webpack)
- **`styles.css`** - Global styles and layout

### WASM Bridge & Core (`wasm/src/`)
- **`WasmBridge.cpp`** - C API implementation (exported functions)
- **`WasmMain.cpp`** - WASM entry point and main loop
- **`WebGPURenderer.cpp`** - GPU draw helpers (per-frame geometry)
- **`WebGPURendererPipelines.cpp`** - Pipelines, bind group, SDF atlas texture, analyzer compute pass
- **`WebGPUContext.cpp`** - WebGPU device initialization
- **`SDL2AudioBackend.cpp`** - Audio callback and buffer management
- **`AudioWorkletEngine.cpp`** - C API of the AudioWorklet engine module (`bespoke_audio_*`, exports in `wasm/exports_audio.txt`)
- **`AudioGraphSnapshotCodec.cpp`** - Snapshot wire format between the two modules
- **`Knob.cpp`** - Rotary control rendering and interaction
- **`WidgetPainter.cpp`** - Knobs, sliders, cables, meters and scopes, drawn once over `Renderer2D` primitives (`drawQuad(ShaderKind, ...)`, paths, text); the backends only map `ShaderKind`s to their pipelines. `tests/RecordingRenderer.h` records calls for unit tests

### WASM Headers (`wasm/include/`)
- **`WasmBridge.h`** - C API declarations
- **`WebGPURenderer.h`** - Renderer interface
- **`WebGPUContext.h`** - GPU context wrapper
- **`SDL2AudioBackend.h`** - Audio backend interface
- **`Knob.h`** - Knob control class

### Build Configuration
- **`wasm/CMakeLists.txt`** - WASM build recipe
- **`webpack.config.js`** - TypeScript/web bundling
- **`tsconfig.json`** - TypeScript compiler options
- **`eslint.config.mjs`** - Linting rules (ESLint 9 flat config, type-aware)

### Shaders (`wasm/shaders/`)
- **`render2d.wgsl`** - All WebGPU render shaders
  - Vertex and fragment shaders for UI controls
  - Knob, slider, button rendering
  - SDF text (`fs_sdf_text`) and 5x7 pixel text (`fs_pixel_text`)
  - Live scope/spectrum (`fs_live_waveform`, `fs_live_spectrum`) reading GPU buffers
- **`analyzer_fft.wgsl`** - Compute FFT for the docked analyzer (WebGPU only; WebGL2 uses `AudioAnalysis::computeSpectrum`)

### Text
- UI text uses an SDF atlas (`wasm/resource-pack/fonts/ui_font.bsdf`, loaded by `SdfFont`) on both backends. Re-bake with `scripts/bake_sdf_font.sh`.
- `?debugFont=pixel` (or `fontFace("pixel")`, used by the font regression panel) keeps the 5x7 pixel font.

## Important Constraints & Gotchas

### 1. WebGPU Capability (with WebGL2 fallback)

The WASM build **defaults to WebGPU** when available:
- Chrome/Edge 113+
- Firefox Nightly with `dom.webgpu.enabled` = true
- Safari 17+ (experimental)

If WebGPU init fails, the app automatically falls back to **WebGL2** unless `?renderer=webgpu` was explicitly requested. That covers shader/pipeline errors, which error scopes catch (`WebGPUContext::pushErrorScopes`). A device lost after init (or a lost WebGL2 context) moves the renderer to WebGL2 on a fresh `#canvas` via `bespoke_reinit_renderer(1)`, keeping the patch. Both canvases are opaque, and screenshots are `canvas.toDataURL()` right after `bespoke_render()`; there is no C++ capture API. Force WebGL2 with `?renderer=webgl` for Playwright screenshots and agent visual inspection.

### 1b. Renderer selection & screenshots

| Mechanism | Example |
|-----------|---------|
| URL | `?renderer=webgl` or `?renderer=webgpu` |
| localStorage | `bespokesynth.renderer` |
| Header dropdown | Persists choice and reloads |
| Keyboard | Ctrl+Shift+R toggles backend |
| C API (before init) | `Module._bespoke_set_renderer_backend(1)` |

```js
await window.__bespoke.captureScreenshot();           // PNG download
await window.__bespoke.captureScreenshot({ x, y, width, height }); // crop
window.__bespoke.getRendererBackend();                // 'webgpu' | 'webgl'
```

Render regression scenes: `?renderTest=1` (synth voice) or `wasm/render_test.html`. `?renderTest=2` is the drum bus (drumseq → mixer → compressor). See [docs/webgl-fallback.md](docs/webgl-fallback.md).

### 2. Shader Language: WGSL

Shaders are in WebGPU Shading Language (WGSL), not GLSL. Syntax differs:
- `fn` instead of `void function()`
- `let/var` instead of `uniform/varying`
- No `#version` directives

Edit `wasm/shaders/*.wgsl` directly. `scripts/embed_shaders.cmake` embeds them into a generated header at build time (`WgslSources.inc`, included by `WebGPURendererPipelines.cpp`); Dawn compiles them at runtime. There is no WGSL string literal in the renderer `.cpp`.

### 3. Audio Threading Model

- The UI module (`BespokeSynthWASM`) runs on the **main thread**; rendering and module graph edits are driven by the TypeScript `requestAnimationFrame` loop in `src/app/renderLoop.ts`.
- **Worklet engine** (`src/audio/workletEngineBackend.ts`, default when `crossOriginIsolated`, `?audio=worklet`): a second, DSP-only module (`BespokeSynthWASM_audio`, `wasm/src/AudioWorkletEngine.cpp`) runs `AudioGraphEngine` inside the AudioWorklet. The UI module posts encoded snapshots on every publish (`AudioGraphSnapshotCodec`) and decoded samples; notes go through a SharedArrayBuffer ring. Params that hold pointers must be rebound in `WasmModuleAdapter::rebindParams` (see `docs/wasm-audio.md`, Option A). Offline bounce still renders with the UI module's engine.
- **Timed notes**: `NoteMessage::timestamp` (AudioContext seconds) makes the engine split the block so a note starts on its frame (`processBlock(..., hostTimeSeconds)`, `test_timed_notes`). QWERTY/MIDI in get start times from `NoteClock` (`src/audio/noteClock.ts`); Script workers write timed notes into their own SAB ring that the worklet drains (`ScriptEngineLink`). Untimed notes and clockless hosts behave as before.
- **RT checks**: the engine block is an `AudioRtScope`; `new`/`delete` and `RtMutex` locks inside it abort in unit tests and are counted in Debug (`rtAllocations`/`rtLocks` in `getAudioHealth()`). Use `RtMutex` for any mutex the UI thread shares with audio state.
- **Worklet ring** (`src/audio/workletRingBackend.ts`, `?audio=ring`, or fallback if the engine fails): the UI module's graph runs on the main thread and fills a SAB ring up to an adaptive lookahead (`src/audio/lookahead.ts`).
- **SDL2 audio** (fallback without COOP/COEP, or `?audio=sdl`) uses Emscripten's synchronous audio callback on the main thread.
- Avoid blocking operations in audio processing paths.
- **Stop is a hard mute**: a stopped transport renders silence. Notes played while stopped fold into a per-key final state that plays once on Play (no burst). A full note ring drops the newest note (`noteDropCount`).
- **Fan-in**: no cable limit; plan steps index flat input vectors sized at compile (see `docs/wasm-audio.md#fan-in`).
- **Control fast path**: control changes travel as `ParamChange` records (SPSC queue / worklet events ring) applied at block start; only topology changes republish and recompile. Adapter params are `constexpr ParamDescriptor` tables (`WasmParams.h`, ids stable) - add a row there, not a hand-written name switch. See `docs/wasm/audio.md#params-and-the-control-fast-path`.
- **Resource lifetime**: samples are addressed by `SampleHandle` (id + generation); `SampleStore` and `LooperArenaPool` evict unpinned buffers LRU against a 256 MB budget after the engine has moved past every snapshot that named them (`ResourceLifetime.h`, `docs/wasm/samples.md`). Report new resources from `WasmModuleAdapter::collectResources`.

### 4. WASM Module Instantiation

The WASM module is loaded **asynchronously**. TypeScript must wait for:
1. Script element load
2. Factory function or `Module.onRuntimeInitialized`
3. Canvas to be ready

Check `src/index.ts` for error handling around module initialization.

### 4b. Heap and host strings

- The heap starts at 32 MB and may grow to the `-sMAXIMUM_MEMORY` cap (2 GiB, `wasm/CMakeLists.txt`). Growth replaces `Module.HEAP*`/`wasmMemory.buffer`, so read views after any call that can allocate rather than caching them.
- Pass strings to C with `module.stringToNewUTF8(str)` and `module._free(ptr)` (`withCStrings` in `src/app/wasmStrings.ts`). `allocateUTF8` is deprecated and no longer exported. The typed surface lives in `wasm/types/bespoke-synth.d.ts`; heap views come from `@types/emscripten`. At runtime only `HEAP8`, `HEAPF32` and `HEAPU8` are exported (`EXPORTED_RUNTIME_METHODS`); the d.ts declares only those, so view `HEAPU8.buffer` as an `Int32Array` after the call (as `src/app/graphMidiOut.ts` does). `_malloc` returns 0 once the heap cannot grow; use `wasmMalloc` (`src/app/wasmStrings.ts`), which throws.

### 5. File System Sandboxing

The WASM runtime has a virtual file system, not direct filesystem access:
- User audio files are imported through the TS shell (drag-and-drop or file picker) and decoded in C++
- Sample bytes persist in the Origin Private File System, keyed by content hash
- Patch JSON stores `extras.sampleHash` references, not PCM
- Use IndexedDB for patch metadata (`src/patchStorage.ts`)
- Use Fetch API for remote assets

### 6. Web MIDI

Browser MIDI is implemented in `src/midi.ts` (device enumeration, permission handling, output + `?midiClock=1` clock) and wired through the header **MIDI** control (`src/ui/midiPanel.ts`: channel filter, output port, QWERTY piano toggle). Note/CC events are forwarded to `_bespoke_midi_note_on`, `_bespoke_midi_note_off`, and `_bespoke_midi_cc` in the WASM bridge. Shift-click a control for CC learn (maps persist in patch JSON).

### 6b. Live instrument surfaces

See [docs/wasm/live-instrument.md](docs/wasm/live-instrument.md). In short:
- **QWERTY piano** (`src/app/qwertyPiano.ts`) keys off `KeyboardEvent.code` and feeds the same path as MIDI in.
- **Undo/redo** is a structured command stack (`wasm/include/BespokeWasm/UndoStack.h`, `ModuleCanvas::undo/redo`). New canvas edits must go through `createModule`/`deleteModule`/`connectModules`/`setModuleControlValue` (or record a command) to be undoable. Automation writes pass `recordUndo=false`.
- **Engine state is pre-sized on the UI thread** after every snapshot publish (`AudioGraphEngine::prepareRuntimeStates` via `ModuleCanvas::setSnapshotListener`). Don't add allocations to `processBlock`.
- **Scala tunings** use `libs/tuning-library` (git submodule). Only `wasm/src/TuningTable.cpp` is built with `-fwasm-exceptions`, and audio reads `GraphMusicScale::noteFrequency`.
- **Input** uses Pointer Events (`bespoke_pointer_*`) for mouse, touch, and pen. Pinch and long-press are recognized in `src/app/inputBridge.ts`.
- **Note processors and MIDI out**: `chorder`, `arpeggiator`, `notefilter` and `noterouter` are `WasmAudioRole::NoteProcessor` adapters (`processNotes`, planned upstream-first). The engine queues the notes it plays for the selected Web MIDI port (`setMidiOutCapture`); `GraphNotePump` in `src/app/graphMidiOut.ts` drains them, and `PerformanceEcho` keeps QWERTY/MIDI-in notes from going out twice. Loading a patch resets the live engine in place (`resetAudioGraphEngineForPatch`) - never swap in a fresh `AudioGraphEngine`, which would not be prepared. Unknown saved types load as `PlaceholderModule`s (see `docs/wasm/audio.md`).
- **Script module** (`?script=1`) runs user JS in a sandboxed worker (`src/script/`). There is no `eval`/`Function` on the main thread.
- **Module help**: `scripts/bake_module_help.py` bakes `resource/tooltips_*.txt` plus `scripts/module_help_wasm.txt` into `resource-pack/help/module_help.json` (`ModuleHelp`). The spawn menu shows descriptions; hovering a control or title bar shows its tooltip (`Module::controlNameAt`, so give new modules an override when controls aren't sliders). Locale comes from `navigator.language`. Give a new module a description and a tooltip per control: CI's `--check` requires that for wave-4 modules and a coverage floor (`MIN_COVERAGE`) for the rest.
- **Demo gallery** (`src/ui/demoGallery.ts`) lists `resource-pack/demos/`, written by `scripts/gen_demos.py`. Each demo is a golden render in `wasm/tests/golden/demos.json` (`test_golden_demos`).
- **Desktop `.bsk` import** (`DesktopSavestate.cpp`, `bespoke_import_desktop_savestate`) maps desktop types and params through `kTypeMap`; everything else loads as a labelled placeholder that round-trips.

### 7. VST Plugin Hosting Unavailable

The WASM build does not support VST2/VST3 plugin hosting (desktop-only feature).

### 8. Cross-Origin Policy

The dev server sets COOP/COEP headers to enable SharedArrayBuffer:
```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

This is required for high-resolution timing. Local dev works; production deployment must maintain these headers.

## Linting & Type Checking

### ESLint

```bash
npm run lint            # Check all TypeScript files
npm run lint -- --fix   # Auto-fix issues where possible
```

Configured in `eslint.config.mjs` (ESLint 9 flat config + `typescript-eslint` 8, type-aware via `tsconfig.json` and `tsconfig.tests.json`). `no-unnecessary-condition` and `no-floating-promises` are errors: call `module._bespoke_*()` directly (everything in `wasm/exports.txt` is always exported, and `npm run test:exports` checks the d.ts against it).

### WASM size budget

`npm run test:size` (`scripts/check_wasm_size.py`) gzips the release `.wasm` files in `wasm/dist` against `wasm/size-budget.json`. It warns over budget and fails more than 10% over. Record a raised allowance in `docs/wasm/module-porting.md`.

### TypeScript

```bash
npm run type-check      # Dry-run type checker (no emit)
npm run build:webpack   # ts-loader type-checks while bundling; no tsc emit
```

Configured in `tsconfig.json` with strict mode enabled. No implicit `any` types allowed.

## Debugging Tips

### Browser DevTools

1. Open DevTools (F12 in Chrome)
2. **Console** tab shows `console.log()` from both TypeScript and C++ code
3. **Sources** tab shows source maps for TypeScript (if `--mode development`)
4. **Performance** tab can profile rendering and audio callbacks

### WASM Debugging

C++ `console.log()` is available via Emscripten's `std::cout`:
```cpp
#include <iostream>
std::cout << "Debug message: " << value << std::endl;
```

Output appears in browser console.

### Common Issues

| Problem | Diagnosis |
|---------|-----------|
| "WebGPU not available" | Browser doesn't support WebGPU. Use Chrome 113+ or Edge. |
| Blank canvas | Check browser console for errors. Verify `bespoke_init()` returned 0 (success). |
| Silent audio | Audio may be muted by browser. Check volume in browser controls. |
| Shader compilation errors | Check `wasm/shaders/render2d.wgsl` for WGSL syntax errors. |
| Module load timeout | WASM download/parse took too long. Check network in DevTools. |

## References

- [Official Bespoke Synth Docs](https://www.bespokesynth.com/docs/)
- [Emscripten Documentation](https://emscripten.org/docs/)
- [WebGPU Specification](https://gpuweb.github.io/gpuweb/)
- [WGSL Spec](https://www.w3.org/TR/WGSL/)
- [SDL2 Audio](https://wiki.libsdl.org/SDL_OpenAudioDevice)
- [docs/wasm/README.md](docs/wasm/README.md) - Canonical WASM contributor guide
- [docs/DEVELOPER_CONTEXT.md](docs/DEVELOPER_CONTEXT.md) - Desktop architecture (for reference, not WASM)

## Build System Internals

### npm build script flow

```
npm run build
  ├─ build:wasm (cd wasm && ./build.sh)   # the only WASM compile
  │  ├─ emcmake cmake (build dir wasm/build/<flavor>)
  │  ├─ cmake --build --parallel <min(nproc, 8), or $BESPOKE_WASM_JOBS>
  │  └─ Replace wasm/dist/ with the new outputs
  └─ build:webpack (webpack --mode production)
     ├─ Load src/index.ts as entry
     ├─ Compile TypeScript via ts-loader (type errors fail the build)
     ├─ Copy wasm/dist → dist/wasm, src/audio/processors → dist/audio
     ├─ Apply HtmlWebpackPlugin
     └─ Output dist/bundle.[hash].js
```

### WASM build script flow

```
wasm/build.sh
  ├─ Source emsdk_env.sh (activate Emscripten)
  ├─ Check emcc >= .emscripten-version and the emdawnwebgpu port
  ├─ emcmake cmake wasm -B wasm/build/<release|debug|profile> \
  │  ├─ -DCMAKE_BUILD_TYPE=<Release|Debug|Profile>
  │  ├─ -DBESPOKE_WASM_BUILD_FLAVOR=<Release|Debug|Profile>
  │  └─ -DBESPOKE_WASM_LTO / _CLOSURE / _SIMD (env vars of the same name, all default ON)
  ├─ cmake --build --parallel <min(nproc, 8), or $BESPOKE_WASM_JOBS>
  └─ After a successful build, wipe and refill wasm/dist/:
     ├─ BespokeSynthWASM.html → index.html
     ├─ BespokeSynthWASM.js
     ├─ BespokeSynthWASM.wasm
     ├─ BespokeSynthWASM.data (preloaded resource pack, mounted at /resource)
     └─ BespokeSynthWASM_audio.js / .wasm (AudioWorklet engine)
```

### Output Structure After Build

```
dist/
├─ index.html             (from src/index.html via webpack)
├─ bundle.[hash].js       (compiled TypeScript)
├─ bundle.[hash].js.map   (source map, --mode development only)
├─ audio/                 (worklet processors from src/audio/processors)
└─ wasm/
   ├─ index.html          (from wasm/dist/, fallback shell)
   ├─ BespokeSynthWASM.js   (Emscripten runtime)
   ├─ BespokeSynthWASM.wasm (binary)
   ├─ BespokeSynthWASM.data (preloaded resource pack; the only copy that ships)
   └─ BespokeSynthWASM_audio.js / .wasm (AudioWorklet engine)
```
