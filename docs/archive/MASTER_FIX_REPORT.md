# BespokeSynth WASM - Master Fix Report

**Date:** 2026-03-15  
**Status:** 🔴 CRITICAL - Multiple blocking issues identified  
**Estimated Fix Time:** 30-45 minutes

---

## Executive Summary

All 4 investigation agents have completed their analysis. The WASM port has **critical compilation errors** that must be fixed before the app can run. Once compilation is fixed, there are **runtime issues** that will prevent audio from working.

### Quick Status
| Component | Status | Blocker? |
|-----------|--------|----------|
| Compilation | 🔴 FAILING | YES |
| WebGPU Init | 🟡 Depends on build | - |
| Shader Panels | 🟡 Depends on build | - |
| Audio Output | 🔴 WILL FAIL (browser policy) | YES |

---

## Critical Issues (Must Fix First)

### 1. 🔴 Git Merge Conflict in WebGPURenderer.h
**File:** `wasm/include/BespokeWasm/WebGPURenderer.h:175-188`  
**Severity:** BLOCKING COMPILATION

```cpp
<<<<<<< HEAD:wasm/include/BespokeWasm/WebGPURenderer.h
      // New drawing methods
      void drawXYPad(float x, float y, float w, float h, float cx, float cy);
      // ... 8 more methods ...
=======
>>>>>>> origin/wasm-renderer-update-17384666709190575130:wasm/include/BespokeWasm/WebGPURenderer.h
```

**Fix:**
```bash
# Keep the new methods (recommended)
sed -i '/<<<<<<< HEAD/,/=======/d' wasm/include/BespokeWasm/WebGPURenderer.h
sed -i '/>>>>>>> origin/d' wasm/include/BespokeWasm/WebGPURenderer.h
```

---

### 2. 🔴 Missing Global Variable Declarations
**File:** `wasm/src/WasmBridge.cpp`  
**Severity:** BLOCKING COMPILATION

Variables used but never declared:
- `gInitState` - Used 10+ times for state tracking
- `gInitErrorMessage` - Used for error reporting
- `gAudioCallbackActive` - Atomic flag for audio callback

**Fix:** Add after line 64 (after `static float gTime = 0.0f;`):
```cpp
// Initialization state
static InitState gInitState = InitState::NotStarted;
static std::string gInitErrorMessage;
static std::atomic<bool> gAudioCallbackActive{false};
```

---

### 3. 🔴 Missing emscripten.h Include
**File:** `wasm/src/WasmBridge.cpp:50`  
**Severity:** BLOCKING COMPILATION

`emscripten_run_script()` called but header not included.

**Fix:** Add after line 13:
```cpp
#include <emscripten.h>  // Required for emscripten_run_script
```

---

### 4. 🔴 Audio Auto-Start Violates Browser Policy
**File:** `wasm/src/WasmBridge.cpp:265-274`  
**Severity:** RUNTIME FAILURE - NO SOUND

Modern browsers require user interaction before audio can play. The current code auto-starts audio during initialization, which will be blocked.

**Fix:** Remove or comment out lines 265-274:
```cpp
// REMOVED: Audio should only start on user interaction
// printf("WasmBridge: Starting audio playback...\n");
// if (!gAudioBackend->start()) {
//     printf("BespokeSynth WASM: Warning - Failed to start audio playback\n");
// }
```

**Note:** The `bespoke_play()` function (called from UI button) will handle starting audio.

---

## Secondary Issues (Fix After Critical)

### 5. 🟡 Audio Callback State Never Reset
**File:** `wasm/src/WasmBridge.cpp:98`  
**Severity:** SHUTDOWN HANG

`gAudioCallbackActive` is set to `true` but never reset to `false`, causing shutdown to spin forever.

**Fix:** At end of `audioCallback()` function:
```cpp
gAudioCallbackActive.store(false);  // ADD THIS
```

---

### 6. 🟡 State Mapping Mismatch in TypeScript
**File:** `src/index.ts:243-264`  
**Severity:** UI PROGRESS DISPLAY BUG

State `0` (NotStarted) has no mapping but is referenced in the completion loop.

**Fix:**
```typescript
const stateToStep: Record<number, string> = {
  0: 'wasm_load',        // NotStarted - ADD THIS
  1: 'webgpu_instance',
  2: 'webgpu_adapter',
  3: 'renderer_pipelines',
  4: 'audio_init',
  5: 'controls_create',
};
```

---

### 7. 🟡 Hardcoded Audio Parameters
**File:** `wasm/src/WasmBridge.cpp:246`  
**Severity:** CODE QUALITY

Function accepts `sampleRate` and `bufferSize` but uses hardcoded values.

**Fix:**
```cpp
// Change:
if (!gAudioBackend->initialize(44100, 512, 2, 0))
// To:
if (!gAudioBackend->initialize(sampleRate, bufferSize, 2, 0))
```

---

### 8. 🟡 CMake Configuration Issues
**File:** `wasm/CMakeLists.txt`  
**Severity:** WARNINGS/POTENTIAL ISSUES

- Duplicate `cmake_minimum_required` and `project()` (lines 15-16)
- `BESPOKE_LIBS_DIR` used before definition (lines 51-52 vs 71)
- Incorrect jsoncpp include path at line 140

**Fix:** See Agent 3 report for detailed CMake fixes.

---

### 9. 🟡 Shader Synchronization Gap
**Files:** `wasm/shaders/render2d.wgsl` vs `wasm/src/WebGPURenderer.cpp`  
**Severity:** MISSING FEATURES

- External .wgsl file has 42 fragment shaders
- C++ embeds only 32 shaders
- 10 shaders are unavailable at runtime

**Fix:** Either:
- Add missing shaders to C++ embedding, OR
- Remove unused shaders from .wgsl file

---

## Verified CORRECT (No Changes Needed)

| Requirement | File | Status |
|-------------|------|--------|
| 5-argument WebGPU callbacks | `WebGPUContext.cpp:18-35` | ✅ CORRECT |
| `WGPU_DEPTH_SLICE_UNDEFINED` usage | `WebGPUContext.cpp:290` | ✅ CORRECT |
| Shader module creation | `WebGPURenderer.cpp:924-930` | ✅ CORRECT |
| EMSCRIPTEN_KEEPALIVE exports | `WasmBridge.h` | ✅ CORRECT |
| InitState state machine | `WasmBridge.cpp:27-36` | ✅ CORRECT |
| Thread safety (atomics) | `SDL2AudioBackend.cpp` | ✅ CORRECT |

---

## Quick Fix Script

Create `/content/build_space/BespokeSynth_WASM/quick_fix.sh`:

```bash
#!/bin/bash
set -e

echo "=== BespokeSynth WASM Quick Fix Script ==="

# 1. Fix merge conflict in WebGPURenderer.h
echo "[1/5] Fixing merge conflict in WebGPURenderer.h..."
sed -i '/<<<<<<< HEAD/,/=======/d' wasm/include/BespokeWasm/WebGPURenderer.h
sed -i '/>>>>>>> origin/d' wasm/include/BespokeWasm/WebGPURenderer.h

# 2. Add missing include to WasmBridge.cpp
echo "[2/5] Adding missing emscripten.h include..."
sed -i '14a #include <emscripten.h>  // Required for emscripten_run_script' wasm/src/WasmBridge.cpp

# 3. Add missing global declarations
echo "[3/5] Adding missing global variable declarations..."
cat > /tmp/fix_globals.txt << 'EOF'

// Initialization state
static InitState gInitState = InitState::NotStarted;
static std::string gInitErrorMessage;
static std::atomic<bool> gAudioCallbackActive{false};
EOF

# Insert after line 64 (after "static float gTime = 0.0f;")
sed -i '65r /tmp/fix_globals.txt' wasm/src/WasmBridge.cpp

# 4. Comment out audio auto-start
echo "[4/5] Disabling audio auto-start (browser policy)..."
sed -i '265,274s/^/\/\/ /' wasm/src/WasmBridge.cpp

# 5. Fix TypeScript state mapping
echo "[5/5] Fixing TypeScript state mapping..."
sed -i "s/1: 'webgpu_instance'/0: 'wasm_load',\n  1: 'webgpu_instance'/" src/index.ts

echo ""
echo "=== Fixes Applied ==="
echo "Run './wasm/build.sh' to test compilation"
```

---

## Testing Checklist

After applying fixes:

- [ ] Run `./wasm/build.sh` - should compile without errors
- [ ] Check browser console - no WebGPU errors
- [ ] Verify InitState reaches 5 (FullyInitialized)
- [ ] Shader panels render correctly
- [ ] Audio plays after clicking play button
- [ ] No Projucer warnings (these are from JUCE examples, not your code)

---

## Expected Console Output (After Fixes)

```
BespokeSynth WASM: Initializing (800x600, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization
WebGPUContext: Adapter found, requesting device
WebGPUContext: Device acquired
WebGPURenderer: Initialization complete
SDL2AudioBackend: Initializing SDL audio...
BespokeSynth WASM: Initialization complete
window.__bespoke_on_init_complete(0)
```

---

## Individual Agent Reports

- **Agent 1:** `agent1_webgpu_report.md` - WebGPU & Renderer
- **Agent 2:** `agent2_bridge_report.md` - WASM Bridge & API
- **Agent 3:** `agent3_build_report.md` - Build System & Shaders
- **Agent 4:** `agent4_audio_report.md` - Audio & Frontend

---

*Generated by 4 parallel investigation agents*  
*Fix immediately to get your shader panels and audio working*
