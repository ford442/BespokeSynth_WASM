# Copilot Instructions for BespokeSynth WASM

This repository contains the WebAssembly port of BespokeSynth, a modular synthesizer designed for live-patching and personalized workflow construction. This document guides Copilot agents on building, testing, and working effectively with this codebase.

## Project Architecture Overview

### Dual Build System (Critical)

This project maintains **TWO distinct architectural branches**:

| Aspect | Desktop | WASM |
|--------|---------|------|
| Framework | JUCE | Emscripten + WebGPU |
| Graphics | OpenGL via NanoVG | WebGPU (custom WGSL shaders) |
| Audio | JUCE Audio Device I/O | SDL2 Audio Backend |
| Entry Point | `Source/MainComponent.cpp` | `wasm/src/WasmMain.cpp` |
| Build Command | `cmake`/`just` | `./wasm/build.sh` |

**Critical Rule**: Changes to `Source/` do not automatically apply to WASM. Always verify WASM compatibility for shared code. The WASM build is currently a proof-of-concept/demo architecture, not feature-parity with desktop.

### Core Design Patterns

- **God Object / Mediator**: `ModularSynth` manages all modules, global state, and event loop
- **Factory Pattern**: `ModuleFactory` instantiates modules by string ID (370+ available on desktop)
- **Observer/Listener**: Extensive use of listener interfaces (`IAudioSource`, `IAudioReceiver`, `INoteReceiver`)
- **Polymorphism**: All modules inherit from `IDrawableModule` (UI + logic combined)

### Key Complexity: Thread Safety

Audio processing runs on a high-priority thread while UI runs on a lower-priority thread:
- Audio graph mutations must hold `ModularSynth::mAudioThreadMutex`
- **Never** modify `mModules` during audio callbacks—use `ModularSynth::LockRender(true)` first
- Circular dependency detection (`FindCircularDependencies()`) prevents feedback loops

## Build and Development Commands

### Prerequisites

**WASM Build**:
- Emscripten SDK 3.1+ with `emdawnwebgpu` port
- CMake 3.16+
- Node.js 18+ and npm 9+

**Desktop Build** (optional):
- CMake 3.16+
- Ninja (recommended)
- Platform-specific tools (see `CONTRIBUTING.md`)

### Build Commands

```bash
# WASM build (primary target for this repo)
./wasm/build.sh                    # Full WASM compile
npm run build:wasm                 # Same via npm

# Full build (WASM + TypeScript + Webpack bundling)
npm run build

# TypeScript + Webpack only (reuse existing WASM binary)
npm run build:web-only

# Development server with hot reload
npm run dev                        # Runs on http://localhost:8080

# Type checking without emit
npm run type-check

# Linting
npm run lint                       # ESLint on TypeScript/JavaScript
# Note: C++ uses clang-format (see Code Style below)
```

### Build Output

- `dist/BespokeSynthWASM.wasm` - WebAssembly binary
- `dist/BespokeSynthWASM.js` - Emscripten glue code
- `dist/index.html` - Bundled HTML with embedded assets

### Testing Strategy

1. **Build verification**: `./wasm/build.sh` must complete without errors
2. **Browser testing**: Chrome 113+, Edge 113+, Firefox Nightly (all with WebGPU enabled)
3. **Console validation**: Check for expected init sequence (see AGENTS.md section 7)
4. **Audio testing**: Verify SDL2 audio context initializes successfully

Expected successful initialization logs:
```
BespokeSynth WASM: Initializing (width×height, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization
WebGPUContext: Adapter found, requesting device
WebGPUContext: Device acquired
WebGPURenderer: Initialization complete
SDL2AudioBackend: Initializing SDL audio...
BespokeSynth WASM: Initialization complete
```

## Code Style and Conventions

### C++ Code

**Formatter**: `clang-format` (config: `.clang-format`)
- **Style**: WebKit-based with customizations
- **Indent**: 3 spaces
- **Braces**: Custom (Allman-like with namespace/class wrapping)
- **Include sorting**: Disabled (preserve manual order)

**Naming**:
| Type | Convention | Example |
|------|-----------|---------|
| Classes | `PascalCase` | `WebGPURenderer` |
| Methods | `camelCase` | `initializeAsync` |
| Member variables | `mPascalCase` | `mDevice` |
| Global functions (C API) | `bespoke_snake_case` | `bespoke_init` |
| Constants | `kCamelCase` | `kVersion` |
| Macros | `UPPER_SNAKE_CASE` | `EMSCRIPTEN_KEEPALIVE` |

**Namespace**: All code in `namespace bespoke { namespace wasm { ... } }`

### TypeScript/JavaScript

**Configuration** (tsconfig.json):
- **Target**: ES2020
- **Strict mode**: Enabled (`strict: true`)
- **Unused checks**: Enabled (`noUnusedLocals`, `noUnusedParameters`)

**Linting** (.eslintrc.js):
- Parser: `@typescript-eslint/parser`
- Extends: ESLint recommended + TypeScript recommended
- `@typescript-eslint/no-explicit-any`: Warn (not error)
- Console logging: Allowed

**Naming**:
- Interfaces: `PascalCase`
- Enums: `PascalCase`
- Variables/functions: `camelCase`

## Key Modules and Entry Points

### WASM Bridge API (C → JavaScript)

Exposed via `EMSCRIPTEN_KEEPALIVE` in `wasm/src/WasmBridge.cpp`:

**Initialization**:
- `bespoke_init(width, height, sampleRate, bufferSize)` - Start async initialization
- `bespoke_shutdown()` - Cleanup
- `bespoke_process_events()` - Process async WebGPU events
- `bespoke_is_fully_initialized()` - Check readiness before render/audio
- `bespoke_get_init_state()` - Return current `InitState` enum value

**Rendering & Input**:
- `bespoke_render()`, `bespoke_resize(width, height)`
- `bespoke_mouse_move/down/up/wheel(...)`
- `bespoke_key_down/up(keyCode, modifiers)`

**Transport**:
- `bespoke_play()`, `bespoke_stop()`
- `bespoke_set_tempo(bpm)`, `bespoke_get_tempo()`

**State Queries**:
- `bespoke_get_panel_count()`, `bespoke_get_panel_name(index)`, `bespoke_is_panel_loaded(index)`

### Initialization State Machine

Tracks 7 states in `enum class InitState`:
```
NotStarted → WebGPURequested → WebGPUReady → RendererReady → AudioReady → FullyInitialized
                                                                            ↑
                                                                          Failed
```

**Critical**: Always check `bespoke_is_fully_initialized()` before rendering or audio operations.

### WebGPU Renderer

Located in `wasm/src/WebGPURenderer.cpp`. Implements a NanoVG-like drawing API:

**Color & State**:
- `fillColor(r, g, b, a)`, `strokeColor(r, g, b, a)`

**Shapes**:
- `rect(x, y, w, h)`, `roundedRect(x, y, w, h, radius)`
- `circle(x, y, radius)`, `beginPath()`, `bezierTo(...)`

**Drawing**:
- `fill()`, `stroke()`, `text(x, y, text)`

**UI Controls**:
- `drawKnob(x, y, size, value, style)` - Rotary knob
- `drawSlider(x, y, w, h, value, range)` - Horizontal slider
- `drawVUMeter(x, y, w, h, level, peak)` - Level meter
- `drawCableWithSag(x1, y1, x2, y2, thickness, color)` - Patch cable with physics

**Shaders**: WGSL source in `wasm/shaders/render2d.wgsl` (~40+ specialized fragment shaders)

### Audio System

**Backend**: SDL2 Audio (`wasm/src/SDL2AudioBackend.cpp`)
- **Sample rate**: Typically 44100 Hz (configurable)
- **Buffer size**: Default 512 samples
- **Thread safety**: Use `std::atomic<bool>` flags for callback synchronization
- Audio callback must complete within buffer duration to avoid underruns

## Important Gotchas and Constraints

### WebGPU Callback Signatures (Critical Fix)

The `emdawnwebgpu` port requires **5-argument callbacks** for adapter/device request:
```cpp
void callback(WGPURequestAdapterStatus status, WGPUAdapter adapter, 
              const char* message, void* userdata, size_t messageSize);
```

Do NOT revert to 4-argument versions. This is a compatibility requirement for current Emscripten versions.

### depthSlice Usage

For 2D rendering operations (no depth testing), always use:
```cpp
WGPU_DEPTH_SLICE_UNDEFINED  // NOT a numeric value
```

### Legacy File Format Support

`.bsk` (JSON savestate) files must remain loadable. Do NOT change JSON serialization keys for existing modules—support migration with fallback logic instead.

### Raw Pointers & Module Deletion

The codebase uses raw pointers for module inter-connections. Ownership is handled by `ModuleContainer`, but dangling pointers can occur during module deletion. Be cautious when traversing module graphs or adding/removing connections.

## Directory Structure Summary

```
BespokeSynth_WASM/
├── Source/                  # Shared core synth code (desktop + WASM)
│   ├── ModularSynth.cpp     # Central mediator (audio loop, module graph)
│   ├── ModuleFactory.cpp    # Module registry
│   ├── IDrawableModule.cpp  # Base class for all modules
│   └── 370+ module implementations
├── wasm/                    # WASM-specific implementation (PRIMARY)
│   ├── build.sh             # Build entry point
│   ├── CMakeLists.txt       # Emscripten build config
│   ├── shell.html           # Emscripten HTML template
│   ├── src/
│   │   ├── WasmBridge.cpp   # Main WASM entry point & C API
│   │   ├── WebGPUContext.cpp # GPU device/adapter management
│   │   ├── WebGPURenderer.cpp # 2D drawing implementation
│   │   ├── SDL2AudioBackend.cpp # Audio callback
│   │   └── Knob.cpp         # Rotary knob UI control
│   ├── shaders/render2d.wgsl # WGSL shader source
│   └── types/bespoke-synth.d.ts # TypeScript declarations
├── src/                     # TypeScript frontend
│   ├── index.ts             # App entry point
│   └── index.html
├── libs/                    # Third-party dependencies (JUCE, jsoncpp, etc.)
├── resource/                # Runtime resources (fonts, presets)
├── package.json             # NPM configuration
├── tsconfig.json            # TypeScript config
├── webpack.config.js        # Bundler config
└── justfile                 # Just task runner (Linux/macOS)
```

## Documentation References

For deeper context, consult these files in order:

1. **AGENTS.md** - Comprehensive agent guide with architecture details, initialization states, shader pipeline, audio system, security considerations
2. **DEVELOPER_CONTEXT.md** - High-level architecture, design patterns, feature map, complexity hotspots, known issues, dependency flows
3. **wasm/README.md** - WASM-specific build instructions, requirements, project structure
4. **CONTRIBUTING.md** - Community contribution guidelines, bug reporting, code submission
5. **README.md** - Project overview, features, basic building instructions

## Development Workflow Tips

### Making C++ Changes

1. Edit files in `wasm/src/` or `Source/` as appropriate
2. Run `./wasm/build.sh` to compile
3. Check browser console for WebGPU/initialization errors
4. Verify initialization logs match expected sequence

### Adding New UI Controls

1. Create control class in `wasm/src/` (follow `Knob.cpp` pattern)
2. Add rendering methods to `WebGPURenderer`
3. Add WGSL shader variants in `wasm/shaders/render2d.wgsl` if needed
4. Expose to JavaScript via new functions in `WasmBridge.cpp`

### Adding New Shaders

1. Edit `wasm/shaders/render2d.wgsl`
2. Embed shader string in `WebGPURenderer.cpp`
3. Create pipeline in `createPipelines()`
4. Add public rendering method that uses the pipeline

### Debugging WebGPU Issues

- Open browser DevTools Console
- Check for WebGPU validation errors
- Verify shader compilation logs
- Confirm initialization state progression matches expected sequence
