# Agent 2: WASM Bridge & API Investigation Report

## Summary
This report documents critical issues found in the BespokeSynth WASM Bridge implementation that prevent successful compilation and proper initialization.

---

## Critical Issues (Blocking Compilation/Runtime)

### 1. [WasmBridge.cpp] Undefined Global Variables
**Location:** `wasm/src/WasmBridge.cpp` - lines 186, 209, 259, 322, 371, 382

**Issue:** Three global variables are used but never declared:
- `gInitState` - Used for state machine tracking
- `gInitErrorMessage` - Used for error reporting  
- `gAudioCallbackActive` - Atomic flag for audio callback tracking

**Current code (usage at line 186):**
```cpp
if (gInitState != InitState::NotStarted)
{
    printf("BespokeSynth WASM: Already initialized or in progress (state=%d)\n", static_cast<int>(gInitState));
    return gInitState == InitState::FullyInitialized ? 0 : 1;
}
```

**Current code (usage at line 371):**
```cpp
for (int i = 0; i < 10 && gAudioCallbackActive.load(); ++i)
{
    // Just a short busy wait
}
```

**Fix:** Add declarations after line 91 (after kVersion):
```cpp
// Version string
static const char* kVersion = "1.0.0-wasm";

// Initialization state (MISSING DECLARATIONS)
static InitState gInitState = InitState::NotStarted;
static std::string gInitErrorMessage;
static std::atomic<bool> gAudioCallbackActive{false};
```

**Impact:** Compilation failure with "undefined reference" or "undeclared identifier" errors.

---

### 2. [WasmBridge.cpp] Missing emscripten.h Include
**Location:** `wasm/src/WasmBridge.cpp` - line 50

**Issue:** `emscripten_run_script()` is called but `<emscripten.h>` is not included.

**Current code (line 50):**
```cpp
emscripten_run_script(script);
```

**Fix:** Add include after line 13:
```cpp
#include <emscripten.h>  // Required for emscripten_run_script
```

**Impact:** Compilation failure - `emscripten_run_script` is undefined.

---

### 3. [WasmBridge.cpp] Audio Callback State Never Reset
**Location:** `wasm/src/WasmBridge.cpp` - lines 98, 371

**Issue:** `gAudioCallbackActive` is set to `true` in the audio callback but never set back to `false`. This causes the shutdown busy-wait loop to spin uselessly.

**Current code:**
```cpp
// Line 98 - Set to true
static void audioCallback(const float* const* input, float* const* output, ...)
{
    gAudioCallbackActive.store(true);  // Never set to false!
    // ...
}

// Line 371 - Shutdown waits for it
for (int i = 0; i < 10 && gAudioCallbackActive.load(); ++i)
{
    // Will spin forever if callback was ever called
}
```

**Fix:** Add at the end of `audioCallback`:
```cpp
static void audioCallback(...)
{
    gAudioCallbackActive.store(true);
    // ... audio processing ...
    gAudioCallbackActive.store(false);  // ADD THIS
}
```

**Impact:** Shutdown may hang or behave unpredictably.

---

## Warnings (Potential Issues)

### 4. [WasmBridge.cpp] InitState Enum Defined in .cpp File
**Location:** `wasm/src/WasmBridge.cpp` - lines 27-36

**Issue:** The `InitState` enum is defined locally in the .cpp file. While this works for internal use, it prevents external debugging tools from understanding the state values.

**Current code:**
```cpp
enum class InitState
{
   NotStarted = 0,
   WebGPURequested = 1,
   WebGPUReady = 2,
   RendererReady = 3,
   AudioReady = 4,
   FullyInitialized = 5,
   Failed = -1
};
```

**Recommendation:** Consider moving to header file if external access is needed, or add a comment that this is intentionally internal.

---

### 5. [WasmBridge.cpp] emscripten_run_script Called in Callback Context
**Location:** `wasm/src/WasmBridge.cpp` - lines 211, 234, 253, 329

**Issue:** `emscripten_run_script()` is called from within async WebGPU callbacks. This is generally safe in Emscripten but can have performance implications.

**Current code (line 211):**
```cpp
emscripten_run_script("if (window.__bespoke_on_init_complete) window.__bespoke_on_init_complete(-1);");
```

**Recommendation:** Consider using `emscripten_async_run_in_main_runtime_thread` for guaranteed main-thread execution, though the current approach may work for simple cases.

---

### 6. [WasmBridge.cpp] Hardcoded Audio Parameters
**Location:** `wasm/src/WasmBridge.cpp` - line 246

**Issue:** Audio initialization uses hardcoded values instead of the parameters passed to `bespoke_init()`.

**Current code:**
```cpp
if (!gAudioBackend->initialize(44100, 512, 2, 0))  // Hardcoded!
```

**Fix:** Should use the parameters:
```cpp
if (!gAudioBackend->initialize(sampleRate, bufferSize, 2, 0))
```

---

### 7. [WasmBridge.cpp] No Validation of WebGPUContext Before Dereferencing
**Location:** `wasm/src/WasmBridge.cpp` - lines 347-351

**Issue:** `bespoke_process_events()` doesn't validate the context before calling methods.

**Current code:**
```cpp
EMSCRIPTEN_KEEPALIVE void bespoke_process_events(void)
{
    if (gContext)  // Good check
    {
        gContext->processEvents();
    }
}
```

**Note:** This is actually correct - the null check is present. Keeping as a positive observation.

---

## InitState Flow Analysis

### State Machine Transitions

```
NotStarted (0)
    │
    ▼ bespoke_init() called
WebGPURequested (1)
    │
    ├──► Failed (-1) [if WebGPU init fails]
    │
    ▼ WebGPU callback success
WebGPUReady (2)
    │
    ├──► Failed (-1) [if renderer init fails]
    │
    ▼ Renderer initialized
RendererReady (3)
    │
    ├──► Failed (-1) [if audio init fails]
    │
    ▼ Audio initialized
AudioReady (4)
    │
    ▼ All demo controls created
FullyInitialized (5)
```

### Critical Path to FullyInitialized

1. `bespoke_init()` called from JavaScript
2. `gContext->initializeAsync()` starts WebGPU initialization
3. WebGPU callback fires on success
4. Renderer is created and initialized
5. Audio backend is created and initialized
6. Demo knobs are created
7. `gInitState = InitState::FullyInitialized` is set (line 322)
8. `gInitialized = true` is set (line 323)

### State Query Functions

The following functions expose the initialization state to JavaScript:

| Function | Line | Returns |
|----------|------|---------|
| `bespoke_get_init_state()` | 1034-1037 | Current InitState as int |
| `bespoke_get_init_error()` | 1039-1042 | Error message string (or empty) |
| `bespoke_is_fully_initialized()` | 1044-1047 | 1 if FullyInitialized, 0 otherwise |

---

## API Export Verification

All critical API functions are properly marked with `EMSCRIPTEN_KEEPALIVE`:

| Function | Line | Status |
|----------|------|--------|
| `bespoke_init` | 180 | ✓ Exported |
| `bespoke_shutdown` | 354 | ✓ Exported |
| `bespoke_render` | 414 | ✓ Exported |
| `bespoke_process_events` | 345 | ✓ Exported |
| `bespoke_mouse_move` | 739 | ✓ Exported |
| `bespoke_mouse_down` | 757 | ✓ Exported |
| `bespoke_mouse_up` | 803 | ✓ Exported |
| `bespoke_mouse_wheel` | 813 | ✓ Exported |
| `bespoke_key_down` | 832 | ✓ Exported |
| `bespoke_key_up` | 857 | ✓ Exported |
| `bespoke_get_init_state` | 1034 | ✓ Exported |
| `bespoke_get_init_error` | 1039 | ✓ Exported |
| `bespoke_is_fully_initialized` | 1044 | ✓ Exported |

**Note:** All expected exports are present. No missing exports detected.

---

## WasmMain.cpp Analysis

**Status:** ✓ **CORRECT**

The `WasmMain.cpp` file is well-structured:
- Properly includes `<emscripten.h>` and `<emscripten/html5.h>`
- Event handlers correctly forward to `WasmBridge` functions
- Main function registers all required callbacks
- Calls `emscripten_exit_with_live_runtime()` to keep the runtime alive

No issues detected in this file.

---

## WasmBridge.h Analysis

**Status:** ✓ **CORRECT**

The header file:
- Properly wraps in `extern "C"` for C linkage
- All functions have `EMSCRIPTEN_KEEPALIVE` attribute
- Includes `<emscripten.h>` for the attribute macro
- Has proper header guards with `#pragma once`

No issues detected in this file.

---

## Fix Recommendations (Priority Order)

### Priority 1: Fix Compilation Errors
1. Add missing `#include <emscripten.h>` to WasmBridge.cpp
2. Add missing global variable declarations:
   ```cpp
   static InitState gInitState = InitState::NotStarted;
   static std::string gInitErrorMessage;
   static std::atomic<bool> gAudioCallbackActive{false};
   ```

### Priority 2: Fix Runtime Issues
3. Fix audio callback to reset `gAudioCallbackActive`:
   ```cpp
   gAudioCallbackActive.store(false);  // At end of audioCallback
   ```

### Priority 3: Code Quality
4. Fix hardcoded audio parameters to use function arguments
5. Review `emscripten_run_script` usage in callback contexts

---

## Files Requiring Changes

| File | Issue | Lines |
|------|-------|-------|
| `wasm/src/WasmBridge.cpp` | Missing includes | After line 13 |
| `wasm/src/WasmBridge.cpp` | Missing variable declarations | After line 91 |
| `wasm/src/WasmBridge.cpp` | Audio callback state bug | Line 127 |

---

## Conclusion

The WASM Bridge implementation has a solid architecture and proper API design, but **three critical issues prevent compilation**:

1. Missing `#include <emscripten.h>`
2. Missing declarations for `gInitState`, `gInitErrorMessage`, and `gAudioCallbackActive`
3. Audio callback state flag never reset

Once these are fixed, the initialization flow should work correctly and reach `InitState::FullyInitialized` (value 5) under normal conditions.

The state machine is well-designed with proper error handling at each stage, and all critical API functions are properly exported for JavaScript consumption.
