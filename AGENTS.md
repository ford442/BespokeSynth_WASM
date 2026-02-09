# Agent Instructions for Bespoke Synth (WASM Focus)

This file contains instructions for AI agents working on the Bespoke Synth project.

## 1. Project Overview

**Bespoke Synth** is a software modular synthesizer designed for live-patching and personalized workflow construction. Unlike traditional DAWs, it treats the canvas as a free-form "patchboard" where modules (oscillators, effects, sequencers) are connected visually and logically.

This repository specifically focuses on the **WebAssembly (WASM) port** of Bespoke Synth, which utilizes **WebGPU** for rendering and **SDL2** for audio. The WASM build is architecturally distinct from the Desktop build.

### Key Differences: Desktop vs WASM

| Aspect | Desktop | WASM |
|--------|---------|------|
| **Framework** | JUCE | Emscripten |
| **Graphics** | NanoVG | WebGPU (custom renderer) |
| **Audio** | JUCE Audio | SDL2 Audio Backend |
| **Entry Point** | `MainComponent.cpp` + `ModularSynth.cpp` | `WasmBridge.cpp` |
| **Module System** | Full `ModularSynth` implementation | Simplified demo architecture |

**Important**: The WASM build is significantly behind the desktop build and serves as a proof-of-concept/demo. Changes to `Source/` do not automatically apply to WASM - always verify WASM compatibility.

## 2. Technology Stack

- **Language**: C++17
- **Build System**: CMake 3.16+
- **WASM Toolchain**: Emscripten SDK 3.1+
- **Graphics APIs**: WebGPU (via `emdawnwebgpu` port)
- **Audio**: SDL2
- **Frontend**: TypeScript, Webpack
- **Shaders**: WGSL (WebGPU Shading Language)

## 3. Project Structure

```
BespokeSynth_WASM/
├── Source/                 # Shared core synth code (370+ modules)
│   ├── ModularSynth.cpp    # Desktop: Main synth engine
│   ├── ModuleFactory.cpp   # Module registry
│   ├── IDrawableModule.cpp # Base class for all modules
│   └── ...                 # 300+ module implementations
│
├── wasm/                   # WASM-specific implementation
│   ├── CMakeLists.txt      # Emscripten build config
│   ├── build.sh            # Build script
│   ├── shell.html          # HTML template with JS integration
│   ├── src/                # WASM C++ sources
│   │   ├── WasmMain.cpp    # Entry point
│   │   ├── WasmBridge.cpp  # JS/C++ bridge (main API)
│   │   ├── WebGPUContext.cpp    # WebGPU device management
│   │   ├── WebGPURenderer.cpp   # 2D rendering implementation
│   │   ├── SDL2AudioBackend.cpp # Audio callback handling
│   │   └── Knob.cpp        # UI control implementation
│   ├── include/BespokeWasm/  # WASM headers
│   │   ├── WasmBridge.h
│   │   ├── WebGPUContext.h
│   │   ├── WebGPURenderer.h
│   │   └── SDL2AudioBackend.h
│   ├── shaders/            # WGSL shader source
│   │   └── render2d.wgsl   # 2D rendering shaders
│   ├── types/              # TypeScript definitions
│   │   └── bespoke-synth.d.ts
│   └── tests/              # Test files
│
├── libs/                   # Third-party dependencies
│   ├── JUCE/               # JUCE framework (subset for WASM)
│   ├── nanovg/             # Vector graphics
│   ├── jsoncpp/            # JSON parsing
│   └── ...                 # Other libraries
│
├── src/                    # TypeScript frontend
│   ├── index.ts            # Main app entry
│   ├── index.html          # HTML template
│   └── styles.css          # Styles
│
├── resource/               # Runtime resources
├── scripts/                # Build/util scripts
├── CMakeLists.txt          # Root CMake config
├── package.json            # NPM configuration
├── tsconfig.json           # TypeScript config
├── webpack.config.js       # Webpack config
└── justfile                # Just command runner (Linux)
```

## 4. Build Commands

### WASM Build (Primary Target)

```bash
# Build WASM (requires Emscripten)
./wasm/build.sh

# Or via npm
npm run build:wasm
npm run build       # Full build (WASM + TypeScript + Webpack)
```

**Requirements for WASM build:**
- Emscripten SDK 3.1+ with `emdawnwebgpu` port
- CMake 3.16+

### Desktop Build (Secondary)

```bash
# Linux/macOS (using just)
just build
just configure Release
just run

# Or using CMake directly
git submodule update --init --recursive
cmake -Bignore/build -DCMAKE_BUILD_TYPE=Release
cmake --build ignore/build --parallel 4
```

### Development Server

```bash
# Webpack dev server with hot reload
npm run dev         # Starts on http://localhost:8080

# Or serve built files
npm run serve       # Python HTTP server on port 8000
```

### NPM Scripts

```bash
npm run build           # Full production build
npm run build:web-only  # TypeScript + Webpack only
npm run dev             # Development server
npm run clean           # Clean build artifacts
npm run clean:all       # Clean including node_modules
npm run lint            # ESLint
npm run type-check      # TypeScript check (no emit)
```

## 5. Code Style Guidelines

### C++ Formatting
- **Formatter**: Use `clang-format` with the project's `.clang-format` file
- **Style**: WebKit-based with customizations
- **Key Settings**:
  - Indent: 3 spaces
  - Brace style: Custom (Allman-like with modifications)
  - Namespace indentation: All
  - Max empty lines: 2
  - Include sorting: Disabled (`SortIncludes: false`)

Example:
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
- **Classes**: `PascalCase` (e.g., `WebGPURenderer`)
- **Methods**: `camelCase` (e.g., `initializeAsync`)
- **Member variables**: `mPascalCase` (e.g., `mDevice`)
- **Global functions** (C API): `bespoke_snake_case` (e.g., `bespoke_init`)
- **Constants**: `kCamelCase` (e.g., `kVersion`)
- **Macros**: `UPPER_SNAKE_CASE`

### TypeScript/JavaScript
- Strict mode enabled
- ES2020 target
- DOM and DOM.Iterable libs
- Interface naming: `PascalCase`
- Enum naming: `PascalCase`

## 6. Architecture Details

### WASM Bridge API (C/C++ → JavaScript)

The `WasmBridge.cpp` exposes a C API using `EMSCRIPTEN_KEEPALIVE`:

```cpp
// Initialization
int bespoke_init(int width, int height, int sampleRate, int bufferSize);
void bespoke_shutdown(void);

// Rendering
void bespoke_render(void);
void bespoke_resize(int width, int height);

// Input
void bespoke_mouse_move(int x, int y);
void bespoke_mouse_down(int x, int y, int button);
void bespoke_key_down(int keyCode, int modifiers);

// Transport
void bespoke_play(void);
void bespoke_stop(void);

// State queries
int bespoke_get_init_state(void);
const char* bespoke_get_init_error(void);
int bespoke_is_fully_initialized(void);
```

### Initialization States

The WASM bridge tracks initialization through 7 states:

```cpp
enum class InitState {
    NotStarted = 0,      // Initial state
    WebGPURequested,     // Async init started
    WebGPUReady,         // Adapter/device acquired
    RendererReady,       // Pipelines created
    AudioReady,          // Audio backend ready
    FullyInitialized,    // All subsystems ready
    Failed               // Initialization failed
};
```

**Critical**: Always check `bespoke_is_fully_initialized()` before rendering or audio operations.

### WebGPU Renderer

The WebGPU renderer implements a subset of NanoVG's API:

- `fillColor()`, `strokeColor()` - Color management
- `rect()`, `roundedRect()` - Basic shapes
- `fill()`, `stroke()` - Drawing operations
- `text()` - Text rendering
- `drawSlider()`, `drawVUMeter()` - UI controls
- `drawCableWithSag()` - Patch cables

### Shader Pipeline

Shaders are written in WGSL and embedded in `WebGPURenderer.cpp`:

- `vs_main` - Vertex shader for 2D rendering
- `fs_solid` - Solid color fragment shader
- `fs_knob_highlight` - 3D radial gradient for knobs
- `fs_wire_glow` - Glowing cable effect
- `fs_slider_track/fill/handle` - Slider components
- And 25+ more specialized shaders

### Thread Safety

- **Audio callback** runs on a separate SDL thread
- Use `std::atomic<bool>` flags for synchronization
- Check `gInitialized` before accessing shared controls in audio callback
- Never modify module graph during audio processing

## 7. Testing Strategy

### WASM Testing

1. **Build verification**: Run `./wasm/build.sh` - must complete without errors
2. **Browser testing**: Test in Chrome 113+, Edge 113+, Firefox Nightly
3. **WebGPU validation**: Check browser console for WebGPU errors
4. **Audio testing**: Verify audio context initialization

### Test Files
- `wasm/tests/test_main.cpp` - Basic WASM test harness

### Expected Console Output (Successful Init)
```
BespokeSynth WASM: Initializing (...)
WasmBridge: starting async WebGPU initialization
WebGPUContext: Adapter found, requesting device
WebGPUContext: Device acquired
WebGPURenderer: Initialization complete
SDL2AudioBackend: Initializing SDL audio...
BespokeSynth WASM: Initialization complete
```

## 8. Common Issues & Solutions

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
- **emdawnwebgpu port**: Use Emscripten 3.1+

### Callback Signature Warning
The `emdawnwebgpu` implementation requires **5-argument callbacks** for `WGPURequestAdapterCallback` and `WGPURequestDeviceCallback`. Do not revert to 4-argument versions.

## 9. Security Considerations

- The WASM virtual file system is sandboxed
- Use IndexedDB for persistent storage (not yet implemented)
- Audio requires user interaction to start (browser policy)
- WebGPU requires secure context (HTTPS or localhost)
- COOP/COEP headers required for SharedArrayBuffer (if threading enabled)

## 10. Development Workflow

### Making Changes

1. **C++ changes**: Edit files in `wasm/src/` or `Source/`
2. **Build**: Run `./wasm/build.sh`
3. **Test**: Open `http://localhost:8000/` after `npm run serve`
4. **Check console**: Look for errors during initialization

### Adding New UI Controls

1. Add control class in `wasm/src/` (follow `Knob.cpp` pattern)
2. Add rendering methods to `WebGPURenderer`
3. Add shader variants if needed
4. Expose to JavaScript via `WasmBridge`

### Adding Shaders

1. Edit `wasm/shaders/render2d.wgsl`
2. Embed shader string in `WebGPURenderer.cpp`
3. Create pipeline in `createPipelines()`
4. Add rendering method that uses the pipeline

## 11. Pre-Commit Checklist

Before submitting changes:

- [ ] Read relevant sections of `DEVELOPER_CONTEXT.md` and `wasm/README.md`
- [ ] Run `./wasm/build.sh` - build completes successfully
- [ ] Check WebGPU callback signatures (5-argument format)
- [ ] Verify depthSlice usage: `WGPU_DEPTH_SLICE_UNDEFINED` for 2D
- [ ] Run `clang-format` on modified C++ files
- [ ] Test in a WebGPU-capable browser
- [ ] Check initialization state tracking is maintained

## 12. Key Resources

| File | Purpose |
|------|---------|
| `wasm/README.md` | WebGPU renderer, shader pipeline, build requirements |
| `wasm/INITIALIZATION_FIXES.md` | Detailed initialization fixes |
| `DEVELOPER_CONTEXT.md` | High-level architecture, complexity hotspots |
| `CONTRIBUTING.md` | General contribution guidelines |
| `wasm/DEBUG_OUTPUT_EXAMPLES.md` | Expected debug output |

## 13. License

GNU General Public License v3.0 - See `LICENSE` file.

---

*Note: This project maintains two distinct codebases. When in doubt, prioritize WASM compatibility for this repository.*
