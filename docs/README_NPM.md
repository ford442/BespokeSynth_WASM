# BespokeSynth WASM - NPM Buildable Web App

This repository contains an npm-buildable web application for BespokeSynth WASM, a modular synthesizer running in the browser with WebGPU rendering and SDL2 audio.

## Quick Start

### Prerequisites

- **Node.js** 18.0.0 or later
- **npm** 9.0.0 or later
- **Emscripten SDK** (for building WASM - optional if you just want to use pre-built WASM)

### Installation

```bash
# Clone the repository
git clone https://github.com/ford442/BespokeSynth_WASM.git
cd BespokeSynth_WASM

# Install dependencies
npm install
```

### Development

```bash
# Start the development server (with hot reload)
npm run dev
```

This will start a webpack dev server at `http://localhost:8080` with hot module replacement.

### Building for Production

```bash
# Build everything (WASM + web app)
npm run build
```

The complete build process:
1. Builds the WASM module from C++ source (requires Emscripten)
2. Compiles TypeScript to JavaScript
3. Bundles everything with webpack

Output will be in the `dist/` directory.

### Build Scripts

- `npm run build` - Full production build (WASM + TypeScript + webpack)
- `npm run build:wasm` - Build only the WASM module (requires Emscripten)
- `npm run build:ts` - Compile TypeScript only
- `npm run build:webpack` - Bundle with webpack only
- `npm run dev` - Start development server with hot reload
- `npm run clean` - Remove all build artifacts
- `npm run type-check` - Type-check TypeScript without building
- `npm run lint` - Run ESLint on TypeScript files

## Project Structure

```
BespokeSynth_WASM/
├── src/                    # TypeScript web app source
│   ├── index.ts           # Main entry point
│   ├── index.html         # HTML template
│   └── styles.css         # Application styles
├── wasm/                   # WebAssembly module
│   ├── src/               # C++ source files
│   ├── include/           # C++ header files
│   ├── types/             # TypeScript definitions
│   ├── build.sh           # WASM build script
│   └── CMakeLists.txt     # CMake configuration
├── dist/                   # Build output (generated)
├── package.json           # NPM configuration
├── tsconfig.json          # TypeScript configuration
└── webpack.config.js      # Webpack configuration
```

## Technology Stack

- **TypeScript** - Type-safe JavaScript for the web app
- **Webpack** - Module bundler and development server
- **WebAssembly** - High-performance audio processing
- **WebGPU** - GPU-accelerated rendering
- **SDL2** - Audio backend
- **Emscripten** - C++ to WebAssembly compiler

## Browser Requirements

### WebGPU Support

BespokeSynth WASM uses WebGPU for rendering. You need a browser with WebGPU support:

- **Chrome/Edge** 113+ (WebGPU enabled by default)
- **Firefox Nightly** (enable `dom.webgpu.enabled` in about:config)
- **Safari** 17+ (experimental support)

### Audio Support

- Web Audio API support
- SharedArrayBuffer support (for multi-threaded audio - optional)

## Configuration

### TypeScript Configuration

Edit `tsconfig.json` to customize TypeScript compiler options.

### Webpack Configuration

Edit `webpack.config.js` to customize:
- Entry points
- Output paths
- Development server settings
- Optimization options

### WASM Build Options

Edit `wasm/CMakeLists.txt` to configure:
- WebGPU rendering (BESPOKE_WASM_WEBGPU)
- SDL2 audio (BESPOKE_WASM_SDL2_AUDIO)
- Threading support (BESPOKE_WASM_THREADS)

## Development Tips

### Hot Reload

The webpack dev server supports hot module replacement. Changes to TypeScript and CSS files will automatically reload in the browser.

### Type Safety

The project includes TypeScript definitions for the WASM module in `wasm/types/bespoke-synth.d.ts`. These provide type-safe access to all WASM functions.

### Debugging

- Use browser DevTools for JavaScript debugging
- Source maps are enabled in development mode
- Console logs show initialization and error messages

### Troubleshooting WebGPU Initialization

If you see "Initialization timed out waiting for WASM to complete":

1. **Check WebGPU support**: Ensure your browser supports WebGPU (Chrome 113+, Edge 113+)
2. **Enable WebGPU**: Some browsers require enabling WebGPU in flags (e.g., `chrome://flags/#enable-unsafe-webgpu`)
3. **Check console logs**: Look for "WebGPUContext: onAdapterRequest called" - if missing, WebGPU async callbacks aren't firing
4. **Verify CORS headers**: If loading from a CDN, ensure proper CORS headers are set
5. **Check hosted files**: Verify both `.js` and `.wasm` files are accessible and match versions

The initialization uses async WebGPU callbacks with event polling. The web app polls `_bespoke_process_events()` every 100ms to ensure callbacks fire. If initialization still fails after 30 seconds, check:
- Browser console for WebGPU errors
- Network tab for failed resource loads
- Whether WebGPU is available: `navigator.gpu !== undefined`

## Building WASM Module

If you want to rebuild the WASM module from source:

1. Install Emscripten SDK:
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. Build the WASM module:
   ```bash
   npm run build:wasm
   ```

3. The output will be in `wasm/dist/`:
   - `BespokeSynthWASM.js` - JavaScript glue code
   - `BespokeSynthWASM.wasm` - WebAssembly binary
   - `index.html` - Emscripten-generated HTML (not used by npm build)

## Deployment

After building:

```bash
npm run build
```

Deploy the contents of the `dist/` directory to any static web server or CDN. Make sure to configure the following headers for SharedArrayBuffer support (if using threading):

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

## License

GNU General Public License v3.0

See [LICENSE](LICENSE) for full text.

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Support

- [Discord](https://discord.gg/YdTMkvvpZZ)
- [GitHub Issues](https://github.com/ford442/BespokeSynth_WASM/issues)
- [Documentation](https://www.bespokesynth.com/docs/)

## Credits

BespokeSynth WASM is based on [BespokeSynth](https://github.com/BespokeSynth/BespokeSynth) by Ryan Challinor and contributors.
