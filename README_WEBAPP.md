# BespokeSynth WASM - TypeScript & AssemblyScript Web App

This is an npm-buildable TypeScript web application with AssemblyScript WASM modules for the BespokeSynth project.

## 🚀 Quick Start

### Prerequisites

- **Node.js** (v18 or later recommended)
- **npm** (comes with Node.js)

### Installation

```bash
# Install dependencies
npm install
```

### Build

```bash
# Build everything (AssemblyScript WASM + TypeScript + Webpack bundle)
npm run build

# Build only AssemblyScript WASM modules
npm run asbuild

# Build only TypeScript
npm run build:ts
```

### Development

```bash
# Start development server with hot reload
npm run dev
```

This will start a webpack dev server at `http://localhost:8080` with hot module replacement enabled.

## 📁 Project Structure

```
.
├── src/                    # TypeScript source files
│   ├── index.ts           # Main TypeScript entry point
│   └── index.html         # HTML template
├── assembly/              # AssemblyScript WASM modules
│   └── index.ts          # Main WASM module
├── build/                # Compiled WASM files (generated)
│   ├── debug.wasm
│   ├── release.wasm
│   └── *.js              # WASM loader files
├── dist/                 # Webpack output (generated)
│   ├── index.html
│   └── bundle.js
├── package.json          # npm package configuration
├── tsconfig.json         # TypeScript configuration
├── asconfig.json         # AssemblyScript configuration
└── webpack.config.js     # Webpack bundler configuration
```

## 🔧 Available Scripts

| Command | Description |
|---------|-------------|
| `npm run build` | Build everything (WASM + TS + bundle) |
| `npm run dev` | Start development server |
| `npm run asbuild` | Build both debug and release WASM |
| `npm run asbuild:debug` | Build debug WASM with source maps |
| `npm run asbuild:release` | Build optimized release WASM |
| `npm run build:ts` | Compile TypeScript only |
| `npm run build:webpack` | Bundle with Webpack |
| `npm run clean` | Remove all build artifacts and dependencies |

## 🎵 About AssemblyScript

[AssemblyScript](https://www.assemblyscript.org/) is a TypeScript-like language that compiles to WebAssembly. It's perfect for:

- **Audio DSP**: Fast audio processing in the browser
- **Performance-critical code**: Near-native performance
- **Familiar syntax**: Write WebAssembly using TypeScript syntax

### Example WASM Functions

The `assembly/index.ts` file includes example functions:

```typescript
// Basic math operations
add(a: i32, b: i32): i32
multiply(a: i32, b: i32): i32

// Audio DSP examples
sineOscillator(phase: f32): f32
calculateRMS(bufferPtr: usize, length: i32): f32
lowPassCoefficient(cutoff: f32): f32
```

## 🏗️ Building for Production

```bash
# Create optimized production build
npm run build

# Output will be in dist/ directory
# Deploy the dist/ folder to your web server
```

## 🔍 Development Tips

1. **WASM Module Updates**: When you modify `assembly/index.ts`, run `npm run asbuild` to recompile
2. **TypeScript Changes**: The dev server will automatically reload on TypeScript changes
3. **Debugging**: Use browser DevTools. Source maps are enabled for both TS and WASM
4. **Performance**: The release WASM build is heavily optimized. Use debug build for development.

## 📚 Next Steps

This setup provides a foundation for building web-based audio applications. You can:

1. **Add Web Audio API integration** for real-time audio processing
2. **Implement DSP algorithms** in AssemblyScript for performance
3. **Create UI components** in TypeScript/HTML
4. **Add MIDI support** for hardware controller integration
5. **Build audio modules** similar to the C++ version

## 🛠️ Technology Stack

- **TypeScript**: Main application logic
- **AssemblyScript**: High-performance WASM modules
- **Webpack**: Module bundler and dev server
- **HTML5**: User interface
- **WebAssembly**: Near-native performance

## 📖 Resources

- [AssemblyScript Documentation](https://www.assemblyscript.org/introduction.html)
- [WebAssembly MDN](https://developer.mozilla.org/en-US/docs/WebAssembly)
- [Web Audio API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Audio_API)
- [TypeScript Handbook](https://www.typescriptlang.org/docs/)

## 📄 License

[GNU GPL v3](LICENSE) - Same as the original BespokeSynth project

## 🤝 Contributing

Contributions are welcome! This web version aims to bring BespokeSynth's modular synthesis capabilities to the browser.

---

**Note**: This is a web-based port using TypeScript and AssemblyScript WASM. The original BespokeSynth C++ codebase is in the `Source/` directory.
