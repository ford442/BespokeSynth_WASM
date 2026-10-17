# Agent Instructions for BespokeSynth WASM

This file contains instructions for AI agents working on the BespokeSynth WASM project.

## 1. Project Overview

**BespokeSynth** is a software modular synthesizer designed for live-patching and personalized workflow construction. This repository specifically focuses on the **WebAssembly (WASM) port** of Bespoke Synth, which runs in web browsers using **WebGPU** (with a WebGL2 fallback) for rendering and an **AudioWorklet** (SDL2 fallback) for audio.

### Dual Build Architecture

This project maintains TWO distinct build targets:

| Aspect | Desktop Build | WASM Build |
|--------|--------------|------------|
| **Framework** | JUCE | Emscripten |
| **Graphics** | OpenGL via NanoVG | WebGPU (custom renderer) |
| **Audio** | JUCE Audio | AudioWorklet SAB ring (default), SDL2 fallback |
| **Build System** | CMake + Ninja | Emscripten + CMake |
| **Entry Point** | `Source/MainComponent.cpp` | `src/app/BespokeSynthApp.ts` drives the `WasmBridge*.cpp` C API (`main()` is a no-op, `INVOKE_RUN=0`) |
| **Module Count** | 370+ modules | ~20 adapter-backed modules (`wasm/src/adapters/`) |

**Important**: The WASM build is architecturally distinct from the Desktop build. Changes to `Source/` do not automatically apply to WASM - always verify WASM compatibility.

## 2. Technology Stack

### Core Technologies
- **Language**: C++17
- **Build System**: CMake 3.16+
- **WASM Toolchain**: Emscripten SDK 6.0.3+ (pinned in `.emscripten-version`) with `emdawnwebgpu` port
- **Graphics API**: WebGPU (via WGSL shaders)
- **Audio**: AudioWorklet + SharedArrayBuffer ring (default), SDL2 fallback
- **Frontend**: TypeScript 5.3+, Webpack 5
- **Shaders**: WGSL (WebGPU Shading Language)

### Third-Party Dependencies (`libs/`)
- **JUCE**: Audio framework (subset for WASM)
- **jsoncpp**: JSON parsing
- **nanovg**: Vector graphics (desktop only)
- **pybind11**: Python bindings (desktop only)
- **ableton-link**: Ableton Link synchronization
- **tuning-library**: Microtonal tuning support
- **exprtk**: Expression evaluation
- **freeverb**: Reverb effect implementation
- And others (see `libs/CMakeLists.txt`)

## 3. Project Structure

```
BespokeSynth_WASM/
├── Source/                      # Shared core synth code (370+ modules)
│   ├── ModularSynth.cpp         # Desktop: Main synth engine
│   ├── ModuleFactory.cpp        # Module registry
│   ├── IDrawableModule.cpp      # Base class for all modules
│   ├── SynthGlobals.cpp         # Global definitions
│   └── ...                      # 300+ module implementations
│
├── wasm/                        # WASM-specific implementation
│   ├── CMakeLists.txt           # Emscripten build configuration
│   ├── build.sh                 # Build script
│   ├── shell.html               # Emscripten HTML template
│   ├── exports.txt              # EXPORTED_FUNCTIONS manifest (every KEEPALIVE symbol)
│   ├── src/                     # WASM C++ sources
│   │   ├── WasmMain.cpp         # No-op main (TS owns the loop)
│   │   ├── WasmBridge{Init,Audio,Input,Render,Patch,Live,LegacyPanels}.cpp  # C API, split by area
│   │   ├── ModuleCanvas*.cpp    # Module graph, rendering, input, undo
│   │   ├── AudioGraphEngine.cpp # RT graph processing (precompiled AudioProcessPlan)
│   │   ├── adapters/            # Per-module WasmModuleAdapter DSP + metadata
│   │   ├── modules/             # Per-module canvas UI
│   │   ├── WebGPUContext.cpp    # WebGPU device management
│   │   ├── WebGPURenderer*.cpp  # WebGPU 2D renderer (draw, pipelines, widgets)
│   │   ├── WebGL2*.cpp          # WebGL2 fallback renderer
│   │   └── SDL2AudioBackend.cpp # SDL2 fallback audio
│   ├── include/BespokeWasm/     # WASM headers (WasmBridge.h, Renderer2D.h, WasmModuleAdapter.h, ...)
│   ├── shaders/                 # render2d.wgsl, analyzer_fft.wgsl, GLSL for WebGL2
│   ├── resource-pack/           # Minimal Emscripten preload set
│   ├── types/                   # TypeScript definitions
│   │   └── bespoke-synth.d.ts
│   └── tests/                   # C++ unit tests (BespokeSynthWASM_test)
│
├── libs/                        # Third-party dependencies
│   ├── JUCE/                    # JUCE framework
│   ├── nanovg/                  # Vector graphics
│   ├── jsoncpp/                 # JSON parsing
│   └── ...                      # Other libraries
│
├── src/                         # TypeScript frontend
│   ├── index.ts                 # Thin entry
│   ├── app/                     # BespokeSynthApp, wasmLoader, inputBridge, renderLoop, publicApi
│   ├── audio/                   # AudioWorklet ring backend, lookahead, health
│   ├── ui/                      # Header panels (MIDI, patch, renderer, script)
│   ├── samples/ script/         # Sample I/O + OPFS, sandboxed script worker
│   └── index.html, styles.css
├── tests/                       # Playwright e2e (tests/e2e) and Node unit tests (tests/unit)
├── docs/                        # docs/wasm/ is canonical for WASM
│
├── resource/                    # Runtime resources (fonts, presets, etc.)
├── scripts/                     # prebuild.js, embed_shaders.cmake, check-*.sh, bake_sdf_font.sh
├── CMakeLists.txt               # Root CMake config (desktop)
├── package.json                 # NPM configuration
├── tsconfig.json                # TypeScript config
├── webpack.config.js            # Webpack configuration
└── justfile                     # Just command runner (Linux)
```

## 4. Build Commands

### Prerequisites

**For WASM Build:**
- Emscripten SDK 6.0.3+ (pinned in `.emscripten-version`) with `emdawnwebgpu` port
- CMake 3.16+
- Node.js 18+ and npm 9+

**For Desktop Build:**
- CMake 3.16+
- Ninja (recommended)
- Platform-specific tools (see `CONTRIBUTING.md`)

### WASM Build (Primary Target)

```bash
# Build WASM only (requires Emscripten)
./wasm/build.sh

# Or via npm (also build:wasm:debug / build:wasm:profile)
npm run build:wasm

# Full build (WASM + TypeScript + Webpack)
npm run build

# TypeScript and Webpack only (no WASM rebuild)
npm run build:web-only
```

### Desktop Build (Secondary)

```bash
# Linux/macOS using just
just build                    # Build with auto-configure
just configure Release        # Configure CMake
just run                      # Build and run

# Or using CMake directly
git submodule update --init --recursive
cmake -Bignore/build -DCMAKE_BUILD_TYPE=Release
cmake --build ignore/build --parallel 4
```

### Development Server

```bash
# Webpack dev server with hot reload (port 8080)
npm run dev

# Serve built files (port 8000)
npm run serve
```

### NPM Scripts

```bash
npm run build           # Full production build
npm run build:wasm      # WASM only (Release; :debug / :profile variants)
npm run build:ts        # TypeScript compilation
npm run build:webpack   # Webpack bundling
npm run build:web-only  # TypeScript + Webpack only
npm run dev             # Development server
npm run clean           # Clean build artifacts
npm run clean:all       # Clean including node_modules
npm run lint            # ESLint
npm run type-check      # TypeScript check (no emit)
npm run test:unit       # C++ unit tests (node wasm/build/release/BespokeSynthWASM_test.js)
npm run test:unit:js    # Node TS unit tests in tests/unit
npm run test:exports    # Check KEEPALIVE symbols vs wasm/exports.txt
npm run test:clang-format
npm run test:e2e        # Playwright (needs dist/)
```

## 5. Code Style Guidelines

### C++ Formatting

- **Formatter**: Use `clang-format` with project's `.clang-format` file
- **Style**: WebKit-based with customizations
- **Key Settings**:
  - Indent: 3 spaces
  - Brace style: Custom (Allman-like with modifications)
  - Namespace indentation: All
  - Max empty lines: 2
  - Include sorting: Disabled (`SortIncludes: false`)

**Example:**
```cpp
namespace bespoke {
   namespace wasm {

      class MyClass {
      public:
         MyClass();
         
         void doSomething();
         
      private:
         int mMemberVariable;
      };

   } // namespace wasm
} // namespace bespoke
```

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Classes | `PascalCase` | `WebGPURenderer` |
| Methods | `camelCase` | `initializeAsync` |
| Member variables | `mPascalCase` | `mDevice` |
| Global functions (C API) | `bespoke_snake_case` | `bespoke_init` |
| Constants | `kCamelCase` | `kVersion` |
| Macros | `UPPER_SNAKE_CASE` | `EMSCRIPTEN_KEEPALIVE` |

### TypeScript/JavaScript

- Strict mode enabled
- ES2020 target
- DOM and DOM.Iterable libs
- Interface naming: `PascalCase`
- Enum naming: `PascalCase`

## 6. Architecture Details

### WASM Bridge API (C/C++ → JavaScript)

`WasmBridge.h` declares a C API using `EMSCRIPTEN_KEEPALIVE`, implemented across `wasm/src/WasmBridge*.cpp`. **Every new exported symbol must be added to `wasm/exports.txt`** (CMake builds `EXPORTED_FUNCTIONS` from it; `npm run test:exports` enforces it). Representative subset:

```cpp
// Initialization
int bespoke_init(int width, int height, int sampleRate, int bufferSize);
void bespoke_shutdown(void);
void bespoke_process_events(void);  // Process async WebGPU events

// Rendering
void bespoke_render(void);
void bespoke_resize(int width, int height);

// Input
void bespoke_mouse_move(int x, int y);
void bespoke_mouse_down(int x, int y, int button);
void bespoke_mouse_up(int x, int y, int button);
void bespoke_mouse_wheel(float deltaX, float deltaY);
void bespoke_key_down(int keyCode, int modifiers);
void bespoke_key_up(int keyCode, int modifiers);

// Transport
void bespoke_play(void);
void bespoke_stop(void);
void bespoke_set_tempo(float bpm);
float bespoke_get_tempo(void);

// State queries
int bespoke_get_init_state(void);
const char* bespoke_get_init_error(void);
int bespoke_is_fully_initialized(void);

// Panel management
int bespoke_get_panel_count(void);
const char* bespoke_get_panel_name(int panelIndex);
int bespoke_is_panel_loaded(int panelIndex);
int bespoke_is_panel_running(int panelIndex);
```

### Initialization States

The WASM bridge tracks initialization through distinct WebGPU and WebGL2 graphics states:

```cpp
enum class InitState {
    NotStarted = 0,      // Initial state
    WebGPURequested = 1, // WebGPU async init started
    WebGPUReady = 2,     // Adapter/device acquired
    RendererReady = 3,   // Pipelines created (both backends)
    AudioReady = 4,      // Audio backend ready
    FullyInitialized = 5,// All subsystems ready
    WebGL2Requested = 6, // WebGL2 context creation started
    WebGL2Ready = 7,     // WebGL2 context acquired
    Failed = -1          // Initialization failed
};
```

**Critical**: Always check `bespoke_is_fully_initialized()` before rendering or audio operations.

### Renderer Abstraction (WebGPU + WebGL2)

All UI code renders through `Renderer2D` (`wasm/include/BespokeWasm/Renderer2D.h`):

| Backend | Implementation | Use case |
|---------|----------------|----------|
| WebGPU (default) | `WebGPURenderer` | Production quality, full WGSL shader set |
| WebGL2 (opt-in) | `WebGL2Renderer` | Debugging, Playwright screenshots, GLSL reference |

**Select backend before init:**

```js
Module._bespoke_set_renderer_backend(1); // 0=WebGPU, 1=WebGL2
```

Or from the browser: `?renderer=webgl`, header dropdown, `localStorage.bespokesynth.renderer`, or **Ctrl+Shift+R** (reloads with the other backend).

If WebGPU init fails and the user did not force `?renderer=webgpu`, `src/index.ts` automatically retries with WebGL2.

### Screenshots for agents & CI

Prefer WebGL2 for reliable canvas readback (`preserveDrawingBuffer` is enabled for explicit `?renderer=webgl`/render-test flows, not for automatic fallback). WebGPU uses GPU texture readback via `bespoke_capture_screenshot()` + `bespoke_get_screenshot_pixels()`.

```js
// After the app is ready
await window.__bespoke.captureScreenshot();
await window.__bespoke.captureScreenshot({ x: 0, y: 40, width: 800, height: 500 }); // crop
window.__bespoke.onScreenshotCaptured((dataUrl, info) => { /* post to agent */ });
```

Keyboard shortcut: **Ctrl+Shift+S**. Header **Screenshot** button also works.

C API (from C++ or via `Module._bespoke_*`):

```js
Module._bespoke_capture_screenshot(widthPtr, heightPtr); // renders frame; returns 1=WebGL2, 0=WebGPU pixels ready
Module._bespoke_get_screenshot_pixels(byteLenPtr);       // RGBA for WebGPU path
```

### Render test / visual regression

Load a canonical module graph (transport, scale, osc→filter→gain→output, LFO→filter CV):

```text
?renderTest=1
?renderer=webgl&renderTest=1   # recommended for PNG diffing
```

```js
Module._bespoke_set_render_test_mode(1);
```

Harness page: `wasm/render_test.html`. Floating renderer FAB appears with `?debug=1`.

See [docs/webgl-fallback.md](docs/webgl-fallback.md) for debug modes, screenshot workflow, and WGSL→GLSL porting notes.

### WebGPU Renderer

The WebGPU renderer implements a NanoVG-like API for 2D UI rendering:

- `fillColor()`, `strokeColor()` - Color management
- `rect()`, `roundedRect()` - Basic shapes
- `fill()`, `stroke()` - Drawing operations
- `text()` - Text rendering
- `drawKnob()`, `drawSlider()`, `drawVUMeter()` - UI controls
- `drawCableWithSag()` - Patch cables with physics simulation

### Shader Pipeline

Shaders are written in WGSL and located in `wasm/shaders/render2d.wgsl` (plus `analyzer_fft.wgsl` compute). `scripts/embed_shaders.cmake` embeds them into a generated `WgslSources.inc` at build time:

- `vs_main` - Vertex shader for 2D rendering
- `fs_solid` - Solid color fragment shader
- `fs_knob_highlight` - 3D radial gradient for knobs
- `fs_wire_glow` - Glowing cable effect
- `fs_slider_track/fill/handle` - Slider components
- `fs_neon_wire` - Animated rainbow wire effect
- `fs_beat_pulse` - Pulsing ring animation
- And 40+ more specialized shaders

### Audio System

- **Backend**: AudioWorklet (`src/audio/workletRingBackend.ts`, default when `crossOriginIsolated`) plays a SharedArrayBuffer ring filled from the main thread; SDL2 is the fallback (`?audio=sdl` or no COOP/COEP)
- **Graph**: `AudioGraphEngine` runs a precompiled process plan; adapters own POD param blocks and runtime-state slots. Do not allocate in `processBlock`
- **Sample Rate**: Typically 44100 Hz
- **Buffer Size**: Configurable (default 512 samples)
- See `docs/wasm/audio.md`

### Modules (Adapters)

Each WASM module is a `WasmModuleAdapter` (`wasm/include/BespokeWasm/WasmModuleAdapter.h`) that declares ports/controls and self-registers with `BESPOKE_REGISTER_MODULE`. Adding a module = one header in `include/BespokeWasm/adapters/`, one source in `src/adapters/`, and one line in `wasm/CMakeLists.txt`. See `docs/wasm/module-porting.md`.

## 7. Testing Strategy

### WASM Testing

1. **Build verification**: Run `./wasm/build.sh` - must complete without errors
2. **Browser testing**: Test in Chrome 113+, Edge 113+, Firefox Nightly
3. **WebGPU validation**: Check browser console for WebGPU errors
4. **Audio testing**: Verify audio context initialization

### Test Files

- `wasm/tests/*.cpp` - C++ unit tests (adapters, canvas, graph engine/contract, patch I/O, DSP tuning) built as `BespokeSynthWASM_test`; run with `npm run test:unit` after `npm run build:wasm`
- `tests/unit/*.test.ts` - Node unit tests (`npm run test:unit:js`)
- `tests/e2e/*.spec.ts` - Playwright e2e (`npm run test:e2e`, see Cursor notes below)

### Expected Console Output (Successful Init)

```
BespokeSynth WASM: Initializing (width×height, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization
WebGPUContext: Adapter found, requesting device
WebGPUContext: Device acquired
SDL2AudioBackend: Initializing SDL audio...
BespokeSynth WASM: Initialization complete
```

## 8. Security Considerations

- The WASM virtual file system is sandboxed
- Patch metadata persists in IndexedDB (`src/patchStorage.ts`); sample bytes in OPFS keyed by content hash
- Audio requires user interaction to start (browser policy)
- WebGPU requires secure context (HTTPS or localhost)
- COOP/COEP headers required for SharedArrayBuffer (configured in webpack)

## 9. Common Issues & Solutions

### WebGPU Initialization Fails
- Check browser compatibility (Chrome/Edge 113+)
- Verify WebGPU flags are enabled
- Check for "WebGPU Required" message in UI

### Audio Issues
- Ensure browser audio permissions are granted
- Check sample rate compatibility (typically 44100 Hz)
- Verify `bespoke_is_fully_initialized()` before audio operations

### Build Errors
- **Missing Emscripten**: Install/activate emsdk
- **CMake version**: Requires 3.16+
- **emdawnwebgpu port**: Use Emscripten 6.0.3+ (see `.emscripten-version`)

### Callback Signature Warning
The `emdawnwebgpu` implementation requires **5-argument callbacks** for `WGPURequestAdapterCallback` and `WGPURequestDeviceCallback`. Do not revert to 4-argument versions.

### depthSlice Usage
Always use `WGPU_DEPTH_SLICE_UNDEFINED` for 2D rendering operations.

## 10. Development Workflow

### Making Changes

1. **C++ changes**: Edit files in `wasm/src/` or `Source/`
2. **Build**: Run `./wasm/build.sh`
3. **Test**: Open `http://localhost:8080/` after `npm run dev`
4. **Check console**: Look for errors during initialization

### Adding New UI Controls

1. Add control class in `wasm/src/` (follow `Knob.cpp` pattern)
2. Add rendering methods to `Renderer2D` and implement in both `WebGPURenderer` and `WebGL2Renderer`
3. Add shader variants in `wasm/shaders/render2d.wgsl` if needed
4. Expose to JavaScript via `WasmBridge`

### Adding Shaders

1. Edit `wasm/shaders/render2d.wgsl`
2. It is embedded automatically at build time (`scripts/embed_shaders.cmake` → `WgslSources.inc`); there are no WGSL string literals in the `.cpp`
3. Create the pipeline in `WebGPURendererPipelines.cpp`
4. Add rendering method that uses the pipeline (and a WebGL2 equivalent in `wasm/shaders/*.frag` / `WebGL2Shaders.cpp`)

## 11. CI/CD

**WASM**: GitHub Actions (`.github/workflows/wasm-ci.yml`) builds WASM, then runs `test:exports`, `test:unit`, `test:clang-format`, `build:web-only`, `type-check`, `lint`, and `test:e2e -- --project=chromium`.

**Desktop**: Azure Pipelines (`azure-pipelines.yml`) with build matrices for macOS (universal), Windows (x64), Linux (x64), and formatting checks.

## 12. Pre-Commit Checklist

Before submitting changes:

- [ ] Read relevant sections of `docs/CONTRIBUTING.md` and `docs/wasm/README.md`
- [ ] Run `./wasm/build.sh` - build completes successfully
- [ ] Check WebGPU callback signatures (5-argument format)
- [ ] Verify depthSlice usage: `WGPU_DEPTH_SLICE_UNDEFINED` for 2D
- [ ] Run `clang-format` on modified C++ files (`npm run test:clang-format`)
- [ ] New `EMSCRIPTEN_KEEPALIVE` symbols listed in `wasm/exports.txt` (`npm run test:exports`)
- [ ] `npm run test:unit` passes
- [ ] Test in a WebGPU-capable browser
- [ ] Check initialization state tracking is maintained

## 13. Key Resources

| File | Purpose |
|------|---------|
| `docs/wasm/README.md` | Canonical WASM contributor entry point, architecture, roadmap, and debugging links |
| `wasm/README.md` | Current build options and renderer/runtime reference |
| `docs/DEVELOPER_CONTEXT.md` | Desktop architecture; links to canonical WASM docs |
| `docs/CONTRIBUTING.md` | General contribution guidelines |
| `docs/CODE_OF_CONDUCT.md` | Community guidelines |

## 14. License

GNU General Public License v3.0 - See `LICENSE` file.

---

*Note: This project maintains two distinct codebases (Desktop and WASM). When in doubt, prioritize WASM compatibility for this repository.*

## Cursor Cloud specific instructions

These notes cover non-obvious gotchas discovered while setting up this environment. Standard commands live in `package.json` scripts and section 4 above.

### Emscripten toolchain (important)
- The pinned `.emscripten-version` is **6.0.3**. WASM sources require `--use-port=emdawnwebgpu` and the modern Dawn WebGPU API (`WGPUStringView`, `WGPURequest*CallbackInfo`, `userdata1/2`). Emscripten 3.1.50 and older fail at compile with `clang++: error: unknown argument: '--use-port=emdawnwebgpu'`.
- `wasm/build.sh` discovers emsdk via `EMSDK`, repo-local `emsdk/`, `$HOME/emsdk`, or an activated `emcc` on `PATH`. If `emcc` is missing, install/activate the pinned version and `source emsdk_env.sh`.
- `npm run build:wasm` writes `wasm/dist/{BespokeSynthWASM.js,.wasm,.data}` (+ `resource/`). It is required before the web app can initialize (no WASM artifacts are committed). The `.data` preload bundle is small (~100 KB, from `wasm/resource-pack/`), not the full desktop `resource/`.

### npm install
- CI uses `npm ci` with the committed `package-lock.json`. Locally, `npm install` is fine when the lockfile is unchanged.

### Running / rendering (headless & browsers)
- Dev server: `npm run dev` serves on `http://localhost:8080` (webpack, with COOP/COEP headers). It starts even without WASM artifacts, but the synth will not initialize until `wasm/dist` exists.
- Headless/VM Chrome cannot create a hardware GL context. The default desktop Chrome fails WebGL2 init with `WebGL2 context creation failed — the canvas may already be bound to WebGPU`, and WebGPU is also unavailable. Use SwiftShader: launch Chromium with `--use-gl=angle --use-angle=swiftshader-webgl` (already set for the `chromium` project in `playwright.config.ts`).
- The reliable way to run/screenshot the app headlessly is **Playwright** (`npm run test:e2e -- --project=chromium`), which serves `dist/` on port `9876` and forces the WebGL2 backend. Requires `npx playwright install chromium` and a prior `npm run build` (or `build:web-only`) so `dist/` exists.
- Useful URL params: `?renderer=webgl` (force WebGL2, best for headless) and `?renderTest=1` (loads a canonical Transport/Scale/Osc→Filter→Gain→Output + LFO patch). Clicking the header **Play** button starts the transport (Output VU meters and the bottom-right scope become active).
