# BespokeSynth WASM - WebGPU Edition

This directory contains the WebAssembly port of BespokeSynth with WebGPU rendering and SDL2 audio backend.

## Features

- **WebGPU Rendering**: Modern GPU-accelerated 2D rendering for smooth UI
- **SDL2 Audio**: Cross-platform audio backend with low latency
- **Knob Controls**: Skeuomorphic rotary knobs with multiple styles
- **Cable/Wire Rendering**: Visual patch cables with realistic sag
- **TypeScript Support**: Full type definitions for JavaScript/TypeScript integration

## Requirements

### For Building

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (version 3.0 or later)
- CMake 3.16 or later
- A modern C++ compiler (for host tools)

> **Note:** Recent Emscripten releases deprecated the `-sUSE_WEBGPU=1` flag. This project now uses the `emdawnwebgpu` port (enabled with `--use-port=emdawnwebgpu`); please use a current Emscripten SDK (3.1+ recommended) when building the WASM target.

### For Running

- A WebGPU-capable browser:
  - Chrome 113+ with WebGPU enabled
  - Edge 113+ with WebGPU enabled
  - Firefox Nightly with `dom.webgpu.enabled` = true
- A web server for local development

## Building

1. Install and activate Emscripten:
   ```bash
   source /path/to/emsdk/emsdk_env.sh
   ```

2. Run the build script:
   ```bash
   ./build.sh
   ```

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

## Project Structure

```
wasm/
├── CMakeLists.txt       # CMake build configuration
├── build.sh             # Build script
├── shell.html           # HTML template
├── include/             # Header files
│   ├── WebGPUContext.h
│   ├── WebGPURenderer.h
│   ├── SDL2AudioBackend.h
│   ├── Knob.h
│   └── WasmBridge.h
├── src/                 # Implementation files
│   ├── WasmMain.cpp
│   ├── WasmBridge.cpp
│   ├── WebGPUContext.cpp
│   ├── WebGPURenderer.cpp
│   ├── SDL2AudioBackend.cpp
│   └── Knob.cpp
├── types/               # TypeScript definitions
│   └── bespoke-synth.d.ts
├── shaders/             # WebGPU shaders (WGSL)
└── tests/               # Test files
    └── test_main.cpp
```

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

1. **Threading**: Multi-threaded audio processing requires SharedArrayBuffer, which needs specific HTTP headers (COOP/COEP).

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
