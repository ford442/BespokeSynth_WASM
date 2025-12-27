# Developer Context & Architectural Overview

## 1. High-Level Architecture & Intent

**Bespoke Synth** is a modular software synthesizer designed for live patching and composition.

### Core Purpose
To provide a highly customizable, canvas-based modular synthesis environment where "everything connects to everything." It supports DSP modules, VST/LV2 hosting, and Python live-coding.

### Tech Stack
*   **Language:** C++17 (Standard).
*   **Framework:** JUCE (Application shell, Audio Device management, VST hosting).
*   **Build System:** CMake.
*   **Scripting:** Python 3 (embedded via `pybind11`).
*   **Graphics:**
    *   **Desktop:** OpenGL 3.2+ with [NanoVG](https://github.com/memononen/nanovg) for vector drawing.
    *   **WASM:** WebGPU with a custom C++ renderer emulating an immediate-mode 2D API.
*   **WASM Toolchain:** Emscripten (emsdk).

### Design Patterns
*   **God Object:** `ModularSynth` (in `Source/ModularSynth.cpp`) is the central singleton that manages the audio graph, UI state, inputs, and file I/O.
*   **Module Pattern:** All modules inherit from `IDrawableModule` (UI) and interfaces like `IAudioSource`, `IAudioReceiver`, `INoteReceiver`.
*   **Factory Pattern:** `ModuleFactory` uses a registry map to instantiate modules by string name (key for saving/loading).
*   **Immediate Mode GUI:** The UI is redrawn every frame (or on demand) using `Draw()` methods in each module, rather than retained widgets.

---

## 2. Feature Map (The "General Points")

### Desktop Application (`Source/`)
*   **Entry Point:** `Source/Main.cpp` (JUCE application startup) -> `Source/MainComponent.cpp` (Window & Audio callback).
*   **Core Engine:** `Source/ModularSynth.cpp`. This is the "brain."
*   **Module System:** `Source/IDrawableModule.h` defines the base class. Modules are in `Source/` (e.g., `Oscillator.cpp`, `FilterButterworth24db.cpp`).
*   **Python Scripting:** `Source/ScriptModule.cpp`. Embeds a Python interpreter to allow live-coding modules.
*   **VST Hosting:** `Source/VSTPlugin.cpp`. Wraps JUCE's VST hosting capabilities.

### WebAssembly Port (`wasm/`)
*   **Status:** **Experimental / Demo.** It is *not* a full port of the Desktop app yet.
*   **Entry Point:** `wasm/src/WasmMain.cpp` (HTML5 event listeners).
*   **Bridge:** `wasm/src/WasmBridge.cpp` exposes C functions to JavaScript and manages the demo state (it does *not* instantiate `ModularSynth`).
*   **Rendering:** `wasm/src/WebGPURenderer.cpp`. A custom-built 2D renderer using WebGPU pipelines to draw shapes, text, and gradients, mimicking the NanoVG API used on desktop.
*   **Audio:** `wasm/src/SDL2AudioBackend.cpp` uses SDL2 to get an audio callback in the browser.

---

## 3. Complexity Hotspots (The "Complex Parts")

### The `ModularSynth` God Class
*   **File:** `Source/ModularSynth.cpp` / `.h`
*   **Why:** It handles *everything*: audio callbacks, mouse/keyboard events, file loading, dependency sorting (to prevent feedback loops), and global state.
*   **Agent Note:** Be extremely careful when modifying this file. Changes here ripple through the entire application. Avoid adding more logic here if it can be encapsulated elsewhere.

### Audio Thread Synchronization
*   **Mechanism:** `mAudioThreadMutex` and `LockRender()`.
*   **Why:** The UI thread (rendering) and Audio thread (DSP) access the same module data.
*   **Danger:** Touching audio graph structures (like adding/removing modules or cables) without locking the audio thread will cause hard crashes.
*   **Agent Note:** Always check thread context. If modifying graph topology, ensure the audio thread is paused or locked.

### WASM WebGPU Renderer
*   **File:** `wasm/src/WebGPURenderer.cpp`
*   **Why:** It manually constructs vertex buffers and manages WebGPU pipelines to draw 2D vector graphics (arcs, beziers, fills). It embeds WGSL shaders as strings.
*   **Gotchas:**
    *   **Zero-initialization:** WebGPU structures (like `WGPURenderPassColorAttachment`) must be strictly initialized. Missing fields (like `depthSlice: WGPU_DEPTH_SLICE_UNDEFINED`) cause validation errors.
    *   **Shader embedding:** Shaders are C++ string constants (`kRender2DShader`). Changes require recompiling the C++ code.

### Python Integration
*   **File:** `Source/ScriptModule.cpp`
*   **Why:** Embeds a full Python interpreter. Data marshalling between C++ and Python (via `pybind11`) can be tricky regarding memory ownership and GIL (Global Interpreter Lock).

---

## 4. Inherent Limitations & "Here be Dragons"

### WASM Limitations
*   **Incomplete Port:** The WASM build (`BespokeSynthWASM`) currently compiles only a subset of core DSP files (Oscillators, Envelopes). It **does not** run the full `ModularSynth` engine. It runs a hardcoded demo defined in `WasmBridge.cpp`.
*   **No NanoVG:** The WASM build cannot use NanoVG directly due to OpenGL/WebGPU differences, hence the custom `WebGPURenderer`.

### Technical Debt
*   **Raw Pointers:** The codebase uses raw pointers (`IDrawableModule*`) extensively for the module graph. Ownership is generally managed by `ModuleContainer`, but be wary of dangling pointers.
*   **Immediate Mode UI Efficiency:** The UI redraws frequently. Complex paths or too many UI elements can degrade performance, especially in the WebGPU implementation which has to rebuild vertex buffers every frame.

### Hard Constraints
*   **Save Format:** `.bsk` files are JSON-based. Breaking backward compatibility with old save files is a major issue. Generally, add new optional fields rather than changing existing ones.
*   **Audio Callback:** The audio callback must remain lock-free (or wait-free) ideally. `ModularSynth` uses a mutex, which is suboptimal but deeply ingrained. Do not introduce *new* locks in the audio path if possible.

---

## 5. Dependency Graph & Key Flows

### Desktop Audio Flow
1.  **Driver:** `MainComponent::audioDeviceIOCallback` (Audio Thread)
2.  **Engine:** `ModularSynth::AudioOut`
    *   Acquires `mAudioThreadMutex`.
    *   advances `gTime`.
3.  **Graph:** Iterates `mSources` (sorted list of `IAudioSource`).
4.  **Module:** `IAudioSource::Process()` -> calls `Process()` on upstream modules recursively or pulls from buffer.
5.  **Output:** Sums to `mOutputBuffers` -> Driver.

### Desktop UI Flow
1.  **Loop:** `MainComponent::render` (on Timer/Vsync).
2.  **State:** `ModularSynth::Poll()` (Updates physics, LFOs, UI state).
3.  **Draw:** `ModularSynth::Draw(NanoVG context)`
    *   Iterates `mModuleContainer`.
    *   Calls `IDrawableModule::DrawModule()` for each visible module.

### WASM Flow (Current Demo)
1.  **Entry:** `bespoke_init` (called from JS).
2.  **Loop:** `bespoke_render` (called from JS `requestAnimationFrame`).
    *   Calls `WebGPURenderer::beginFrame`.
    *   Manually draws demo UI (Knobs, Panels) via `gRenderer`.
    *   Calls `WebGPURenderer::endFrame` -> `wgpuQueueSubmit`.
3.  **Audio:** `SDL2AudioBackend` callback -> `audioCallback` (in `WasmBridge.cpp`).
    *   Generates Sine wave based on global `gKnobs` state.
