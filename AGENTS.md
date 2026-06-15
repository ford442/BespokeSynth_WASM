# Agent Instructions for BespokeSynth WASM

This file contains instructions for AI agents working on the BespokeSynth WASM project.

## 1. Project Overview

**BespokeSynth** is a software modular synthesizer designed for live-patching and personalized workflow construction. This repository specifically focuses on the **WebAssembly (WASM) port** of Bespoke Synth, which runs in web browsers using **WebGPU** for rendering and **SDL2** for audio.

### Dual Build Architecture

This project maintains TWO distinct build targets:

| Aspect | Desktop Build | WASM Build |
|--------|--------------|------------|
| **Framework** | JUCE | Emscripten |
| **Graphics** | OpenGL via NanoVG | WebGPU (custom renderer) |
| **Audio** | JUCE Audio | SDL2 Audio Backend |
| **Build System** | CMake + Ninja | Emscripten + CMake |
| **Entry Point** | `Source/MainComponent.cpp` | `wasm/src/WasmMain.cpp` |
| **Module Count** | 370+ modules | Subset (demo architecture) |

**Important**: The WASM build is architecturally distinct from the Desktop build. Changes to `Source/` do not automatically apply to WASM - always verify WASM compatibility.

## 2. Technology Stack

### Core Technologies
- **Language**: C++17
- **Build System**: CMake 3.16+
- **WASM Toolchain**: Emscripten SDK 3.1+ with `emdawnwebgpu` port
- **Graphics API**: WebGPU (via WGSL shaders)
- **Audio**: SDL2
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
│   ├── src/                     # WASM C++ sources
│   │   ├── WasmMain.cpp         # Entry point
│   │   ├── WasmBridge.cpp       # JS/C++ bridge (main API)
│   │   ├── WebGPUContext.cpp    # WebGPU device management
│   │   ├── WebGPURenderer.cpp   # 2D rendering implementation
│   │   ├── SDL2AudioBackend.cpp # Audio callback handling
│   │   └── Knob.cpp             # UI control implementation
│   ├── include/BespokeWasm/     # WASM headers
│   │   ├── WasmBridge.h
│   │   ├── WebGPUContext.h
│   │   ├── WebGPURenderer.h
│   │   ├── SDL2AudioBackend.h
│   │   └── Knob.h
│   ├── shaders/                 # WGSL shader source
│   │   └── render2d.wgsl        # 40+ specialized shaders
│   ├── types/                   # TypeScript definitions
│   │   └── bespoke-synth.d.ts
│   └── tests/                   # Test files
│       └── test_main.cpp
│
├── libs/                        # Third-party dependencies
│   ├── JUCE/                    # JUCE framework
│   ├── nanovg/                  # Vector graphics
│   ├── jsoncpp/                 # JSON parsing
│   └── ...                      # Other libraries
│
├── src/                         # TypeScript frontend
│   ├── index.ts                 # Main app entry
│   ├── index.html               # HTML template
│   └── styles.css               # Styles
│
├── resource/                    # Runtime resources (fonts, presets, etc.)
├── scripts/                     # Build/util scripts
│   └── prebuild.js              # Cross-platform prebuild
├── CMakeLists.txt               # Root CMake config (desktop)
├── package.json                 # NPM configuration
├── tsconfig.json                # TypeScript config
├── webpack.config.js            # Webpack configuration
└── justfile                     # Just command runner (Linux)
```

## 4. Build Commands

### Prerequisites

**For WASM Build:**
- Emscripten SDK 3.1+ with `emdawnwebgpu` port
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

# Or via npm
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
npm run build:wasm      # WASM only
npm run build:ts        # TypeScript compilation
npm run build:webpack   # Webpack bundling
npm run build:web-only  # TypeScript + Webpack only
npm run dev             # Development server
npm run clean           # Clean build artifacts
npm run clean:all       # Clean including node_modules
npm run lint            # ESLint
npm run type-check      # TypeScript check (no emit)
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

The `WasmBridge.cpp` exposes a C API using `EMSCRIPTEN_KEEPALIVE`:

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

Or from the browser: `?renderer=webgl`, header dropdown, or `localStorage.bespokesynth.renderer`.

See [docs/webgl-fallback.md](../docs/webgl-fallback.md) for debug modes, screenshot workflow, and WGSL→GLSL porting notes.

### WebGPU Renderer

The WebGPU renderer implements a NanoVG-like API for 2D UI rendering:

- `fillColor()`, `strokeColor()` - Color management
- `rect()`, `roundedRect()` - Basic shapes
- `fill()`, `stroke()` - Drawing operations
- `text()` - Text rendering
- `drawKnob()`, `drawSlider()`, `drawVUMeter()` - UI controls
- `drawCableWithSag()` - Patch cables with physics simulation

### Shader Pipeline

Shaders are written in WGSL (WebGPU Shading Language) and located in `wasm/shaders/render2d.wgsl`:

- `vs_main` - Vertex shader for 2D rendering
- `fs_solid` - Solid color fragment shader
- `fs_knob_highlight` - 3D radial gradient for knobs
- `fs_wire_glow` - Glowing cable effect
- `fs_slider_track/fill/handle` - Slider components
- `fs_neon_wire` - Animated rainbow wire effect
- `fs_beat_pulse` - Pulsing ring animation
- And 40+ more specialized shaders

### Audio System

- **Backend**: SDL2 Audio
- **Callback**: Thread-safe audio callback with atomic flags
- **Sample Rate**: Typically 44100 Hz
- **Buffer Size**: Configurable (default 512 samples)
- **Thread Safety**: Use `std::atomic<bool>` flags for synchronization

## 7. Testing Strategy

### WASM Testing

1. **Build verification**: Run `./wasm/build.sh` - must complete without errors
2. **Browser testing**: Test in Chrome 113+, Edge 113+, Firefox Nightly
3. **WebGPU validation**: Check browser console for WebGPU errors
4. **Audio testing**: Verify audio context initialization

### Test Files

- `wasm/tests/test_main.cpp` - Basic WASM test harness covering:
  - Math operations
  - Memory allocation
  - String operations
  - Vector operations
  - Audio buffer simulation

### Expected Console Output (Successful Init)

```
BespokeSynth WASM: Initializing (width×height, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization
WebGPUContext: Adapter found, requesting device
WebGPUContext: Device acquired
WebGPURenderer: Initialization complete
SDL2AudioBackend: Initializing SDL audio...
BespokeSynth WASM: Initialization complete
```

## 8. Security Considerations

- The WASM virtual file system is sandboxed
- Use IndexedDB for persistent storage (not yet implemented)
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
- **emdawnwebgpu port**: Use Emscripten 3.1.50+

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
2. Add rendering methods to `WebGPURenderer`
3. Add shader variants in `wasm/shaders/render2d.wgsl` if needed
4. Expose to JavaScript via `WasmBridge`

### Adding Shaders

1. Edit `wasm/shaders/render2d.wgsl`
2. Embed shader string in `WebGPURenderer.cpp`
3. Create pipeline in `createPipelines()`
4. Add rendering method that uses the pipeline

## 11. CI/CD

The project uses Azure Pipelines (`azure-pipelines.yml`) with build matrices for:
- macOS (x64/arm64 universal binary)
- Windows (x64)
- Linux (x64)
- Code formatting checks

## 12. Pre-Commit Checklist

Before submitting changes:

- [ ] Read relevant sections of `CONTRIBUTING.md` and `DEVELOPER_CONTEXT.md`
- [ ] Run `./wasm/build.sh` - build completes successfully
- [ ] Check WebGPU callback signatures (5-argument format)
- [ ] Verify depthSlice usage: `WGPU_DEPTH_SLICE_UNDEFINED` for 2D
- [ ] Run `clang-format` on modified C++ files
- [ ] Test in a WebGPU-capable browser
- [ ] Check initialization state tracking is maintained

## 13. Key Resources

| File | Purpose |
|------|---------|
| `wasm/README.md` | WebGPU renderer, shader pipeline, build requirements |
| `WASM_FIX_SUMMARY.md` | Technical details of WASM fixes |
| `DEPLOYMENT_GUIDE.md` | Deployment instructions for WASM files |
| `DEVELOPER_CONTEXT.md` | High-level architecture, complexity hotspots |
| `CONTRIBUTING.md` | General contribution guidelines |
| `CODE_OF_CONDUCT.md` | Community guidelines |

## 14. License

GNU General Public License v3.0 - See `LICENSE` file.

---

*Note: This project maintains two distinct codebases (Desktop and WASM). When in doubt, prioritize WASM compatibility for this repository.*
