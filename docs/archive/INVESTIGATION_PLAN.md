# WASM Port Failure Investigation Plan

## Objective
Identify root causes preventing BespokeSynth WASM from compiling and running successfully, specifically focusing on:
- C++ compilation errors
- Projucer warnings
- Runtime initialization failures
- Shader panel visibility
- Audio output

## Agent Assignments

### Agent 1: WebGPU & Renderer Investigation
**Focus Area:** `wasm/src/WebGPUContext.cpp`, `wasm/src/WebGPURenderer.cpp`, `wasm/shaders/`

**Key Checks:**
1. WebGPU callback signatures (MUST be 5-argument format for emdawnwebgpu)
2. `WGPU_DEPTH_SLICE_UNDEFINED` usage for 2D rendering
3. Shader compilation and pipeline creation
4. Renderer initialization sequence
5. Any C++ syntax errors or missing includes

**Expected Output:**
- List of callback signature issues
- Shader compilation errors
- Renderer init failure points
- Specific line numbers for fixes

---

### Agent 2: WASM Bridge & Module System
**Focus Area:** `wasm/src/WasmBridge.cpp`, `wasm/src/WasmMain.cpp`, module integration

**Key Checks:**
1. EMSCRIPTEN_KEEPALIVE function signatures
2. InitState transitions and tracking
3. Module factory integration with WASM build
4. C++ API boundary issues
5. Memory management between JS and C++

**Expected Output:**
- API mismatches
- State machine issues
- Module loading failures
- Memory safety concerns

---

### Agent 3: Build System & Shader Pipeline
**Focus Area:** `wasm/CMakeLists.txt`, build scripts, shader embedding

**Key Checks:**
1. CMake configuration for Emscripten
2. emdawnwebgpu port configuration
3. Shader string embedding in C++
4. Build script errors
5. Missing dependencies or flags

**Expected Output:**
- CMake configuration errors
- Missing compiler flags
- Shader embedding issues
- Build script problems

---

### Agent 4: Audio Backend & Frontend Integration
**Focus Area:** `wasm/src/SDL2AudioBackend.cpp`, `src/index.ts`, TypeScript/C++ bridge

**Key Checks:**
1. SDL2 audio initialization
2. Audio callback thread safety
3. TypeScript to C++ call mappings
4. Frontend initialization sequence
5. Audio context browser requirements

**Expected Output:**
- Audio init failure points
- Thread safety issues
- TS/C++ API mismatches
- Browser integration problems

---

## Success Criteria
- App compiles without C++ errors
- No Projucer warnings
- WebGPU initializes successfully
- Shader panels render
- Audio produces sound
- InitState reaches FullyInitialized

## Report Format
Each agent should provide:
1. **Critical Issues** (blocking compilation/initialization)
2. **Warnings** (potential runtime issues)
3. **Fix Recommendations** (specific code changes)
4. **File Locations** (line numbers where applicable)
