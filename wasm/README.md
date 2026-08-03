# BespokeSynth WASM - WebGPU Edition

This directory contains the WebAssembly port of BespokeSynth with WebGPU rendering and SDL2 audio backend.

## Recent Updates

**🔧 Initialization Stability Improvements** - The WASM port now includes comprehensive fixes for startup reliability:
- Explicit initialization state tracking
- Thread-safe audio callback handling  
- Enhanced error handling and validation
- Health monitoring during startup

See [INITIALIZATION_FIXES.md](INITIALIZATION_FIXES.md) for detailed information about the improvements.

## Features

- **WebGPU Rendering**: Modern GPU-accelerated 2D rendering for smooth UI (default)
- **WebGL2 Fallback**: Opt-in reference renderer for debugging and automated screenshots (`?renderer=webgl`)
- **Shared Renderer2D API**: Module canvas, knobs, and demo panels render through one interface
- **SDL2 Audio**: Cross-platform audio backend with low latency
- **Knob Controls**: Skeuomorphic rotary knobs with multiple styles
- **Cable/Wire Rendering**: Visual patch cables with realistic sag
- **TypeScript Support**: Full type definitions for JavaScript/TypeScript integration

## Requirements

### For Building

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) **6.0.3** (pinned in [`.emscripten-version`](../.emscripten-version) and used by CI)
- CMake 3.16 or later
- A modern C++ compiler (for host tools)

> **Note:** This project uses the `emdawnwebgpu` port (`--use-port=emdawnwebgpu`) and the modern Dawn WebGPU API. Emscripten **3.1.50 and older do not build** this tree. Install the pinned SDK version (or newer) from `.emscripten-version`.

### For Running

- **WebGPU path (default):** Chrome 113+, Edge 113+, or Firefox Nightly with `dom.webgpu.enabled`
- **WebGL2 fallback:** Any browser with WebGL2 — use `?renderer=webgl` for debugging without WebGPU

See [docs/webgl-fallback.md](../docs/webgl-fallback.md) for renderer selection, debug modes, and screenshot capture.

## Building

1. Install and activate the pinned Emscripten SDK:
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   EMSCRIPTEN_VERSION="$(tr -d '[:space:]' < /path/to/BespokeSynth_WASM/.emscripten-version)"
   ./emsdk install "$EMSCRIPTEN_VERSION"
   ./emsdk activate "$EMSCRIPTEN_VERSION"
   source ./emsdk_env.sh
   ```

   `wasm/build.sh` also discovers emsdk via `EMSDK`, a repo-local `emsdk/`, `$HOME/emsdk`, or an already-activated `emcc` on `PATH`.

2. Run the build script. Release is the default; Debug and Profile use separate CMake build directories:
   ```bash
   ./build.sh             # Release: -O3, ASSERTIONS=0, Asyncify off
   ./build.sh Debug       # -O0, -g, ASSERTIONS=2
   ./build.sh Profile     # -O2, -g, frame-instrumentation compile flag
   ```

   The equivalent npm commands are `npm run build:wasm`, `npm run build:wasm:debug`, and
   `npm run build:wasm:profile`. Set `BESPOKE_WASM_JOBS` to override the bounded default
   parallelism (the lesser of CPU count and 8).

   `CMakePresets.json` contains matching `wasm-release`, `wasm-debug`, and `wasm-profile`
   presets for IDEs and manual CMake use. Invoke them through `emcmake cmake --preset wasm-debug`.

   Or build manually:
   ```bash
   mkdir build && cd build
   emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build . --parallel
   ```

3. Find the output in `dist/`:
   - `index.html` - Main HTML page
   - `BespokeSynthWASM.js` - JavaScript glue code
   - `BespokeSynthWASM.wasm` - WebAssembly binary

## Running Locally

Start a local web server:
```bash
cd dist
python -m http.server 8000
```

Then open `http://localhost:8000/` in your browser.

Note: The `shell.html` template includes a default JavaScript handler that will automatically call `Module._bespoke_init` when the Emscripten runtime is ready and will display helpful UI messages if WebGPU initialization fails or times out.

## Bundled resources and demo patches

The build preloads `wasm/resource-pack/` (not the full desktop `resource/` tree) into the
Emscripten filesystem at `/resource`. This keeps the transferable `.data` file small while
still bundling `savestate/wasm-starter.bsk` for `?patch=starter`. See
[docs/wasm/resources.md](../docs/wasm/resources.md).
`bespoke_load_layout("savestate/wasm-starter.bsk")` loads the portable starter patch; use
`?patch=starter` to load it on startup. The matching browser helper is
`window.__bespoke.loadBundledLayout("savestate/wasm-starter.bsk")`.

Desktop `.bsk` examples and `layouts/blank.json` may refer to desktop-only module types, so they
remain shipped as reference demos but are intentionally rejected unless their graph is supported by
the WASM module registry.

## Project Structure

```
wasm/
├── CMakeLists.txt       # CMake build configuration
├── build.sh             # Build script
├── shell.html           # HTML template
├── include/             # Public WASM include root
│   ├── BespokeWasm/     # Canonical project headers
│   │   ├── WebGPUContext.h
│   │   ├── WebGPURenderer.h
│   │   ├── WebGL2Context.h
│   │   ├── WebGL2Renderer.h
│   │   ├── Renderer2D.h
│   │   ├── SDL2AudioBackend.h
│   │   ├── Knob.h
│   │   └── WasmBridge.h
│   └── webgpu/          # Vendored WebGPU C headers
├── src/                 # Implementation files
│   ├── WasmMain.cpp
│   ├── WasmBridge.cpp
│   ├── WebGPUContext.cpp
│   ├── WebGPURenderer.cpp
│   ├── WebGL2Context.cpp
│   ├── WebGL2Renderer.cpp
│   ├── SDL2AudioBackend.cpp
│   └── Knob.cpp
├── types/               # TypeScript definitions
│   └── bespoke-synth.d.ts
├── shaders/             # WebGPU shaders (WGSL)
│   └── render2d.wgsl    # 2D rendering shaders for UI controls
└── tests/               # Test files
    └── test_main.cpp
```

## Include Conventions

WASM project headers live under `wasm/include/BespokeWasm/`. Source and headers should include them with the explicit namespace path:

```cpp
#include "BespokeWasm/Knob.h"
#include "BespokeWasm/Renderer2D.h"
```

Do not add duplicate project headers directly under `wasm/include/`. `wasm/include` is the CMake include root; `BespokeWasm/` is the canonical project-header namespace. Root-level files such as `exprtk.hpp`, `Tunings.h`, `juce_compat.h`, and `json/` are narrow compatibility forwarders for shared desktop `Source/` files that include those third-party names directly.

## WebGPU Shaders

The `shaders/render2d.wgsl` file contains all the WGSL shaders for rendering control panel UI elements:

### Core Shaders
- `vs_main` - Vertex shader for 2D rendering
- `fs_solid` - Solid color fragment shader
- `fs_textured` - Textured rendering with alpha blending

### Knob & Wire Shaders
- `fs_knob_highlight` - 3D radial gradient for knob controls
- `fs_wire_glow` - Glowing cable/wire effect
- `fs_connection_pulse` - Animated pulse along connections

### Slider Shaders
- `fs_slider_track` - 3D inset slider track
- `fs_slider_fill` - Animated gradient slider fill
- `fs_slider_handle` - Metallic slider thumb/handle

### Button Shaders
- `fs_button` - 3D bevel button with pressed state
- `fs_button_hover` - Pulsing hover glow effect

### Toggle Switch Shaders
- `fs_toggle_switch` - Toggle track with rounded ends
- `fs_toggle_thumb` - 3D toggle handle

### Envelope & Display Shaders
- `fs_adsr_envelope` - ADSR envelope fill display
- `fs_adsr_grid` - Grid background for envelope editors
- `fs_waveform` - Waveform line with glow
- `fs_waveform_filled` - Filled waveform visualization

### Spectrum Analyzer Shaders
- `fs_spectrum_bar` - Frequency bar with color gradient
- `fs_spectrum_peak` - Peak hold indicator

### Panel & Background Shaders
- `fs_panel_background` - Rounded corner panel with gradient
- `fs_panel_bordered` - Panel with border highlight

### Text Effect Shaders
- `fs_text_glow` - Pulsing text glow effect
- `fs_text_shadow` - Soft text shadow

### Additional Control Shaders
- `fs_progress_bar` - Animated striped progress bar
- `fs_scope_display` - CRT-style oscilloscope trace
- `fs_scope_grid` - Oscilloscope grid overlay
- `fs_led_indicator` - 3D LED indicator (lit state)
- `fs_led_off` - LED indicator (off state)
- `fs_dial_ticks` - Rotary dial tick marks
- `fs_fader_groove` - Fader slot/groove
- `fs_fader_cap` - Metallic fader handle
- `fs_mod_wheel` - Modulation wheel texture
- `fs_vu_meter` - VU meter segment

## API Reference

### JavaScript API

```javascript
// Initialize
const result = Module._bespoke_init(width, height, sampleRate, bufferSize);

// Control playback
Module._bespoke_play();
Module._bespoke_stop();

// Render frame
Module._bespoke_render();

// Handle input
Module._bespoke_mouse_down(x, y, button);
Module._bespoke_mouse_up(x, y, button);
Module._bespoke_mouse_move(x, y);
Module._bespoke_key_down(keyCode, modifiers);
```

### TypeScript Usage

```typescript
import { createBespokeSynth } from './types/bespoke-synth';

const canvas = document.getElementById('canvas') as HTMLCanvasElement;
const synth = await createBespokeSynth(canvas, {
    sampleRate: 44100,
    bufferSize: 512
});

synth.play();
synth.startRenderLoop();
```

## Knob Styles

The `Knob` class supports multiple visual styles:

1. **Classic** - Traditional synth knob with pointer line
2. **Vintage** - Metal knob with knurling texture
3. **Modern** - Flat design with arc indicator
4. **LED** - LED ring around the knob
5. **Minimal** - Simple dot indicator

## Configuration Options

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BESPOKE_WASM_WEBGPU` | ON | Enable WebGPU rendering |
| `BESPOKE_WASM_SDL2_AUDIO` | ON | Enable SDL2 audio backend |
| `BESPOKE_WASM_THREADS` | OFF | Enable threading (experimental) |
| `BESPOKE_WASM_ASYNCIFY` | OFF | Enable Asyncify only for a future async C++ call stack |
| `BESPOKE_WASM_BUILD_FLAVOR` | Release | Release, Debug, or Profile flags and assertions |

### Build-size comparison

Measure the generated `.wasm` artifact after a clean build; debug symbols intentionally make
the Debug artifact much larger. The CI release artifact is the deployment baseline.

| Build | Optimization | Assertions | Asyncify | Expected size relationship |
|---|---|---:|---:|---|
| Release | `-O3` | 0 | off | Smallest production artifact |
| Profile | `-O2 -g` | 1 | off | Larger than Release; keeps symbols and frame instrumentation flag |
| Debug | `-O0 -g` | 2 | off | Largest; use only for local debugging |

### Runtime Configuration

Audio and rendering settings can be configured via the JavaScript API or URL parameters.

## Browser Compatibility

| Browser | Version | WebGPU | Status |
|---------|---------|--------|--------|
| Chrome | 113+ | ✅ | Supported |
| Edge | 113+ | ✅ | Supported |
| Firefox | Nightly | ⚠️ | Experimental |
| Safari | 17+ | ⚠️ | Experimental |

## Known Limitations

1. **Threading**: Multi-threaded audio processing requires SharedArrayBuffer, which needs specific HTTP headers (COOP/COEP). The development server sends those headers to support the experimental threaded preset, but the shipping WASM build keeps `BESPOKE_WASM_THREADS=OFF` until a tested pthread audio path is enabled.

2. **File System**: The virtual file system is sandboxed. Use IndexedDB for persistent storage.

3. **MIDI**: WebMIDI support is not yet implemented (planned for future release).

4. **VST Plugins**: Plugin hosting is not available in the WASM build.

## License

GNU General Public License v3.0

Copyright (C) 2024

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
