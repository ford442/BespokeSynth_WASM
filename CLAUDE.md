# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Quick Start

### Development Environment
- **Node.js**: 18+ required (check via `node --version`)
- **npm**: 9+ (check via `npm --version`)
- **Emscripten SDK**: Required for WASM builds (see [Building WASM](#building-wasm))

### Common Commands

**TypeScript/Web Development**:
```bash
npm install                 # Install dependencies (run once)
npm run build              # Full build: WASM + TypeScript + Webpack
npm run build:web-only     # Rebuild TypeScript only (faster iteration)
npm run dev                # Start dev server at http://localhost:8080
npm run serve              # Serve dist/ directory on port 8000
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
   - Not part of WASM build

2. **WASM/Web Build** (`wasm/` directory)
   - Simplified port for browser
   - Emscripten toolchain
   - WebGPU rendering
   - SDL2 audio backend
   - Currently **architecturally independent** from desktop build

**Key Constraint**: Changes in `Source/` do not automatically apply to WASM. The WASM build is a distinct branch with its own modules, rendering, and audio systems.

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

1. Install Emscripten SDK:
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh  # Required before every build
   ```

2. Verify installation:
   ```bash
   emcc --version  # Should show version 3.0+
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
   - `wasm/dist/BespokeSynthWASM.wasm` - WebAssembly binary (~3-5 MB)

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
- **`WebGPURenderer.cpp`** - GPU rendering (large file, ~2000 lines)
- **`WebGPUContext.cpp`** - WebGPU device initialization
- **`SDL2AudioBackend.cpp`** - Audio callback and buffer management
- **`Knob.cpp`** - Rotary control rendering and interaction

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
- **`.eslintrc.js`** - Linting rules

### Shaders (`wasm/shaders/`)
- **`render2d.wgsl`** - All WebGPU shaders
  - Vertex and fragment shaders for UI controls
  - Knob, slider, button rendering
  - Waveform and spectrum visualizations

## Important Constraints & Gotchas

### 1. WebGPU Capability Required

The WASM build **requires WebGPU** support:
- Chrome/Edge 113+
- Firefox Nightly with `dom.webgpu.enabled` = true
- Safari 17+ (experimental)

Older browsers will fail during `bespoke_init()`.

### 2. Shader Language: WGSL

Shaders are in WebGPU Shading Language (WGSL), not GLSL. Syntax differs:
- `fn` instead of `void function()`
- `let/var` instead of `uniform/varying`
- No `#version` directives

Edit `wasm/shaders/render2d.wgsl` directly. Shaders are compiled at runtime.

### 3. Audio Threading Model

- WebAssembly runs on a **single thread**
- Audio callbacks are synchronous (no Worker threads currently)
- Avoid blocking operations in audio processing
- The render loop and audio callback are strictly serialized

### 4. WASM Module Instantiation

The WASM module is loaded **asynchronously**. TypeScript must wait for:
1. Script element load
2. Factory function or `Module.onRuntimeInitialized`
3. Canvas to be ready

Check `src/index.ts` for error handling around module initialization.

### 5. File System Sandboxing

The WASM runtime has a virtual file system, not direct filesystem access:
- Cannot read user's local files directly
- Use IndexedDB for persistent storage
- Use Fetch API for remote assets

### 6. MIDI Not Yet Implemented

WebMIDI support is planned but not currently in the codebase. Input is mouse/keyboard only.

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

Configured in `.eslintrc.js`. Rules enforce strict mode, no unused variables, consistent naming.

### TypeScript

```bash
npm run type-check      # Dry-run type checker (no emit)
npm run build:ts        # Compile and emit to dist/
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
- [DEVELOPER_CONTEXT.md](DEVELOPER_CONTEXT.md) - Desktop architecture (for reference, not WASM)

## Build System Internals

### npm build script flow

```
npm run build
  ├─ prebuild (scripts/prebuild.js if exists)
  ├─ build:wasm (cd wasm && ./build.sh)
  │  ├─ emcmake cmake ..
  │  ├─ cmake --build . --parallel 55
  │  └─ Copy outputs to wasm/dist/
  ├─ build:ts (tsc)
  │  └─ Emit .js and .d.ts to dist/
  └─ build:webpack (webpack --mode production)
     ├─ Load src/index.ts as entry
     ├─ Compile TypeScript via ts-loader
     ├─ Copy wasm/dist → dist/wasm
     ├─ Apply HtmlWebpackPlugin
     └─ Output dist/bundle.[hash].js
```

### WASM build script flow

```
wasm/build.sh
  ├─ Source emsdk_env.sh (activate Emscripten)
  ├─ mkdir build && cd build
  ├─ emcmake cmake .. \
  │  ├─ -DCMAKE_BUILD_TYPE=Release
  │  ├─ -DBESPOKE_WASM_WEBGPU=ON
  │  └─ -DBESPOKE_WASM_SDL2_AUDIO=ON
  ├─ cmake --build . --parallel 55
  └─ Copy to wasm/dist/:
     ├─ BespokeSynthWASM.html → index.html
     ├─ BespokeSynthWASM.js
     ├─ BespokeSynthWASM.wasm
     └─ resource/ (if exists)
```

### Output Structure After Build

```
dist/
├─ index.html             (from src/index.html via webpack)
├─ bundle.[hash].js       (compiled TypeScript)
├─ bundle.[hash].js.map   (source map)
└─ wasm/
   ├─ index.html          (from wasm/dist/, fallback shell)
   ├─ BespokeSynthWASM.js   (Emscripten runtime)
   ├─ BespokeSynthWASM.wasm (binary)
   └─ resource/           (assets if present)
```
