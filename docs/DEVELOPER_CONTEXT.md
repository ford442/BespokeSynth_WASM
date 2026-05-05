# Developer Context & Architectural Overview

## 1. High-Level Architecture & Intent

**Core Purpose**
Bespoke Synth is a software modular synthesizer designed for live-patching and personalized workflow construction. Unlike traditional DAWs, it treats the canvas as a free-form "patchboard" where modules (oscillators, effects, sequencers) are connected visually and logically.

**Tech Stack**
*   **Language:** C++17
*   **Frameworks:**
    *   **Desktop:** JUCE (handles windowing, audio device I/O, VST hosting).
    *   **WASM:** Emscripten, WebGPU (rendering), SDL2 (audio backend).
*   **Graphics:**
    *   **Desktop:** NanoVG (vector graphics library) wrapped for JUCE.
    *   **WASM:** Custom WebGPU renderer implementing a subset of the drawing API.
*   **Scripting:** Python (embedded for live-coding modules).
*   **Build System:** CMake.

**Design Patterns**
*   **God Object / Mediator:** `ModularSynth` acts as the central hub managing all modules, global state, and the main event loop.
*   **Factory Pattern:** `ModuleFactory` handles the instantiation of all synth modules by string ID.
*   **Observer/Listener:** Extensive use of listener interfaces (e.g., `IAudioSource`, `IAudioReceiver`, `INoteReceiver`) for inter-module communication.
*   **Polymorphism:** All modules inherit from `IDrawableModule`, which combines UI (`DrawModule`) and logic.

## 2. Feature Map

| Feature | Description | Entry Point / Key File |
| :--- | :--- | :--- |
| **Core Engine** | Manages the main loop, audio callbacks, and module graph. | `Source/ModularSynth.cpp` |
| **Module System** | Base class for all synth modules. Handles common UI and state. | `Source/IDrawableModule.cpp` |
| **Audio Processing** | Interface for anything that produces sound. | `Source/IAudioSource.h` |
| **Module Creation** | Registry and spawner for all available modules. | `Source/ModuleFactory.cpp` |
| **Save/Load** | JSON-based state persistence. | `Source/SaveStateLoader.cpp`, `ofxJSONElement` |
| **Python Scripting** | Embedded Python environment for custom scripts. | `Source/ScriptModule.cpp` |
| **VST Hosting** | Hosting VST2/VST3 plugins. | `Source/VSTPlugin.cpp` |
| **WASM Bridge** | Separate entry point for the web version. | `wasm/src/WasmBridge.cpp` |

## 3. Complexity Hotspots

### A. Thread Safety & Audio Locking
*   **Context:** The application runs a high-priority audio thread and a lower-priority UI thread.
*   **Mechanism:** `NamedMutex mAudioThreadMutex` protects shared resources.
*   **Danger:** Touching the module graph (adding/removing modules) during audio processing will crash the synth. `ModularSynth::LockRender(true)` is used to pause audio processing during graph mutations.
*   **Agent Note:** *Never* modify `mModules` or connection pointers without holding the render lock. Verify thread context before calling audio functions.

### B. Circular Dependency Detection
*   **Context:** Feedback loops in audio connections can cause infinite recursion or blown filters.
*   **Mechanism:** `FindCircularDependencies()` traverses the audio graph to detect loops.
*   **Complexity:** It involves a graph traversal algorithm that must run efficiently to update UI feedback (cables turning red).

### C. The Desktop vs. WASM Split
*   **Context:** The project maintains two distinct rendering and system backends.
*   **Complexity:**
    *   **Desktop** uses `MainComponent.cpp` and NanoVG.
    *   **WASM** uses `WasmBridge.cpp`, `WebGPURenderer.cpp`, and does **not** compile the full `ModularSynth` class yet. It runs a simplified "demo" architecture in the current state.
*   **Agent Note:** Do not assume changes in `Source/` automatically apply to the WASM build. The WASM build is currently a distinct architectural branch.

## 4. Inherent Limitations & "Here be Dragons"

### Known Issues
*   **WASM Parity:** The WASM build is significantly behind the desktop build. It is architecturally decoupled and currently serves as a proof-of-concept/demo.
*   **VST Support:** VST hosting is platform-dependent and often fragile due to the closed-source nature of many plugins.

### Technical Debt
*   **`ModularSynth` Size:** The `ModularSynth` class is massive (thousands of lines). It handles too many responsibilities (UI events, audio routing, file I/O). Refactoring this is high-risk.
*   **Raw Pointers:** The codebase uses many raw pointers for module inter-connections. Ownership is generally handled by the `ModuleContainer`, but dangling pointers are a risk during module deletion.

### Hard Constraints
*   **Legacy Layouts:** Support for loading older `.bsk` (JSON) files must be maintained. Do not change JSON serialization keys for existing modules.

## 5. Dependency Graph & Key Flows

### Critical Path: Audio Generation
1.  **Hardware Callback:** `ModularSynth::AudioOut` is called by the audio device.
2.  **Output Buffers:** Iterates through `mOutputBuffers`.
3.  **Graph Traversal:** `IAudioSource::GetSample` is called recursively up the chain.
4.  **Processing:** Each module computes its sample (or retrieves a cached one for the current buffer index).

### Critical Path: User Interaction
1.  **Input:** `MainComponent` receives mouse/keyboard events from JUCE.
2.  **Dispatch:** Events are forwarded to `ModularSynth`.
3.  **Routing:** `ModularSynth` checks which module is under the cursor or has focus.
4.  **Module Action:** `IDrawableModule::OnMouse*` methods are triggered.
5.  **State Change:** Module updates internal parameters (often atomic or mutex-protected).
