# Agent Instructions for Bespoke Synth (WASM Focus)

This file contains instructions for AI agents (Jules, Gemini, Copilot) working on the Bespoke Synth project.

## 1. Mission Context
You are working on **Bespoke Synth**, a modular synthesizer. Your primary focus is the **WebAssembly (WASM) port** which utilizes **WebGPU** for rendering and **SDL2** for audio.

**Important**: The WASM build is architecturally distinct from the Desktop build.
*   **Desktop**: Uses JUCE, NanoVG, and a full `ModularSynth` implementation.
*   **WASM**: Uses Emscripten, WebGPU, and a `WasmBridge` that manages a limited subset of functionality.
*   **Code Sharing**: While some core logic in `Source/` is shared, do *not* assume that changes to `Source/` will automatically work or compile in WASM. Always verify.

## 2. Key Resources
Before starting complex tasks, index these files:
*   `DEVELOPER_CONTEXT.md`: High-level architecture, feature map, and "complexity hotspots".
*   `wasm/README.md`: Specifics on the WebGPU renderer, shader pipeline, and build requirements.
*   `CONTRIBUTING.md`: General contribution guidelines and process.
*   `wasm/DEBUG_OUTPUT_EXAMPLES.md`: Examples of expected debug output (if available).

## 3. Directives & Constraints

### A. Build & Verification (Mandatory)
You **must** verify that your changes compile for the WASM target.
*   **Command**: Run `./wasm/build.sh` from the root directory.
*   **Environment**: The environment is pre-configured with Emscripten.
*   **Failure**: If the build fails, your task is incomplete. Diagnose the compiler error (often related to missing headers or platform-specific code in `Source/`).

### B. Code Style
*   **Formatter**: Adhere to the conventions defined in `.clang-format`.
*   **Style**: WebKit style (as defined in the config).
*   **Action**: If you are unsure about formatting, run `clang-format` on the files you modified.

### C. Architecture & "Gotchas"
*   **WebGPU Shaders**: Shader source code is located in `wasm/shaders/`. When modifying `.wgsl` files, ensure they are correctly embedded into `WebGPURenderer.cpp` (or however the build system handles them - check `wasm/README.md`).
*   **Callback Signatures**: The `emdawnwebgpu` implementation requires 5-argument callbacks for `WGPURequestAdapterCallback` and `WGPURequestDeviceCallback`. Do not revert these to standard WebGPU 4-argument versions.
*   **Depth Slice**: For 2D rendering in WebGPU, `depthSlice` must be `WGPU_DEPTH_SLICE_UNDEFINED` (or `0xFFFFFFFF`).
*   **Split Codebase**: The WASM build excludes `MainComponent.cpp` and `ModularSynth.cpp` (the desktop core). It uses `WasmBridge.cpp` instead.

## 4. Pre-Commit Checklist
Before submitting your changes, ensure:
1.  [ ] You have read the relevant sections of `DEVELOPER_CONTEXT.md`.
2.  [ ] You have run `./wasm/build.sh` and it completed successfully.
3.  [ ] You have checked that your changes do not break the 5-argument WebGPU callback signatures.
4.  [ ] You have formatted your code according to `.clang-format`.

## 5. Helpful Commands
*   **Build WASM**: `./wasm/build.sh`
*   **List Files (Recursive)**: `find . -maxdepth 3 -not -path '*/.*'`
*   **Check Environment**: `emcc --version`

---
*Note: If you encounter a situation where the Desktop build instructions conflict with WASM instructions, prioritize the **WASM** outcome for this session.*
