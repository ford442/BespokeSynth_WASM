# Agent 4: Audio & Frontend Investigation Report

## Critical Issues (Blocking)

### 1. **MISSING: Global Variable Declarations [wasm/src/WasmBridge.cpp:1-36]**
**Status:** COMPILATION ERROR - WILL NOT BUILD

The following global variables are **used but never declared** in `WasmBridge.cpp`:

| Variable | Type | Used At | Purpose |
|----------|------|---------|---------|
| `gInitState` | `static InitState` | Lines 186, 188, 189, 194, 208, 217, 322, etc. | Tracks initialization progress |
| `gInitErrorMessage` | `static std::string` | Lines 209, 233, 252, 337, 383, 1041 | Stores error messages |
| `gAudioCallbackActive` | `static std::atomic<bool>` | Lines 98, 371 | Tracks audio callback state |

**Current code (missing declarations):**
```cpp
// Global state section ends at line 64, missing:
// static InitState gInitState = InitState::NotStarted;
// static std::string gInitErrorMessage;
// static std::atomic<bool> gAudioCallbackActive{false};
```

**Fix: Add after line 64 (after `static float gTime = 0.0f;`):**
```cpp
// Initialization state
static InitState gInitState = InitState::NotStarted;
static std::string gInitErrorMessage;
static std::atomic<bool> gAudioCallbackActive{false};
```

---

### 2. **CRITICAL: Audio Auto-Start Violates Browser Policy [wasm/src/WasmBridge.cpp:265-274]**
**Status:** RUNTIME FAILURE - Audio will not play in modern browsers

The code attempts to start audio automatically during initialization:

```cpp
// Start audio playback
printf("WasmBridge: Starting audio playback...\n");
if (!gAudioBackend->start())  // <-- This is called without user interaction!
{
    printf("BespokeSynth WASM: Warning - Failed to start audio playback\n");
}
```

**Problem:** Modern browsers (Chrome 66+, Firefox 66+, Safari 14+) require user interaction (click/tap) before audio can play. The `AudioContext` will start in a "suspended" state.

**Impact:**
- Audio initialization may appear successful but produces no sound
- `gAudioBackend->start()` may return `true` but audio is suspended at browser level
- VU meters will show no activity

**Fix:** 
1. Remove automatic audio start from `bespoke_init()`
2. Rely on `bespoke_play()` API called from user interaction handler
3. Add audio context state check in frontend:

```cpp
// In bespoke_init() - REMOVE these lines (265-274):
// printf("WasmBridge: Starting audio playback...\n");
// if (!gAudioBackend->start()) { ... }
```

---

### 3. **MEDIUM: State-to-Step Mapping Mismatch [src/index.ts:243-264]**
**Status:** INCORRECT UI PROGRESS REPORTING

The TypeScript `pollInitState()` function maps C++ states to UI steps:

```typescript
const stateToStep: Record<number, string> = {
  1: 'webgpu_instance',  // WebGPURequested
  2: 'webgpu_adapter',   // WebGPUReady (adapter acquired)
  3: 'renderer_pipelines', // RendererReady
  4: 'audio_init',       // AudioReady
  5: 'controls_create',  // FullyInitialized
};
```

**Problems:**
1. State `0` (NotStarted) is not mapped but appears in INIT_STEPS as first entry
2. The loop `for (const s of stateOrder)` marks all previous states complete, but:
   - State `0` has no step mapping → `stateToStep[0]` is undefined
   - When state is 1, it tries to complete `stateToStep[0]` → undefined

**Fix:** Add mapping for state 0 or exclude it from completion loop:
```typescript
// Option 1: Add mapping
const stateToStep: Record<number, string> = {
  0: 'wasm_load',        // NotStarted (during init)
  1: 'webgpu_instance',  // WebGPURequested
  // ... rest
};

// Option 2: Fix the loop
for (const s of stateOrder) {
  if (s < state && stateToStep[s]) {  // Add check for undefined
    if (!this.completedSteps.has(stateToStep[s])) {
      this.completeStep(stateToStep[s]);
    }
  }
}
```

---

### 4. **LOW: Buffer Size Parameter Ignored [wasm/src/WasmBridge.cpp:246]**
**Status:** CODE QUALITY ISSUE

The `bespoke_init()` function accepts `bufferSize` parameter but hardcodes it:

```cpp
EMSCRIPTEN_KEEPALIVE int bespoke_init(int width, int height, int sampleRate, int bufferSize)
{
    // ...
    if (!gAudioBackend->initialize(44100, 512, 2, 0))  // <-- Ignores parameters!
    {
```

**Fix:** Use the passed parameters:
```cpp
if (!gAudioBackend->initialize(sampleRate, bufferSize, 2, 0))
```

---

### 5. **MEDIUM: Missing Audio Context Resume Handling [src/index.ts:440-454]**
**Status:** POTENTIAL AUDIO FAILURE ON RESTART

The play button handler calls `bespoke_play()` but doesn't handle AudioContext suspension:

```typescript
if (playBtn) {
  playBtn.addEventListener('click', () => {
    if (this.module._bespoke_play) {
      this.module._bespoke_play();
    }
  });
}
```

**Problem:** If the browser suspends the audio context (e.g., after tab switch), simply calling `SDL_PauseAudioDevice(device, 0)` may not work - the Web Audio API AudioContext needs to be resumed via user interaction.

**Fix:** Add AudioContext state handling (requires exposing SDL audio context or using Emscripten SDL2 audio hooks).

---

## Audio Init Flow Analysis

```
User Opens Page
      ↓
[index.ts] DOMContentLoaded → BespokeSynthApp.init()
      ↓
[index.ts] loadWasmModule() → Loads BespokeSynthWASM.js
      ↓
[index.ts] Module._bespoke_init(width, height, 44100, 512)
      ↓
[WasmBridge.cpp] bespoke_init()
      ↓
Set gInitState = WebGPURequested (1)
      ↓
WebGPUContext::initializeAsync() → Returns immediately with started=true
      ↓
Return 1 to JS (async init started)
      ↓
[index.ts] waitForAsyncInit() → Sets up polling
      ↓
[Async Callback] WebGPU ready → Set state = WebGPUReady (2)
      ↓
Create WebGPURenderer → Set state = RendererReady (3)
      ↓
Create SDL2AudioBackend → initialize(44100, 512, 2, 0)
      ↓
  [SDL2AudioBackend.cpp] SDL_Init(SDL_INIT_AUDIO)
  [SDL2AudioBackend.cpp] SDL_OpenAudioDevice(...)
  [SDL2AudioBackend.cpp] Allocate buffers
      ↓
Set state = AudioReady (4)
      ↓
⚠️ gAudioBackend->start() ← AUTO-START (VIOLATES BROWSER POLICY)
      ↓
Create UI knobs
      ↓
Set state = FullyInitialized (5)
      ↓
Call window.__bespoke_on_init_complete(0)
      ↓
[index.ts] Setup event listeners, start render loop
```

**Critical Observation:** The audio auto-start at state 4 happens without user interaction, which will fail in all modern browsers.

---

## TS/C++ API Mismatches

### API Availability Checks
The TypeScript code correctly checks for API availability before calling:

| C++ Function | TS Check | Status |
|--------------|----------|--------|
| `_bespoke_init` | `this.module._bespoke_init?.()` | ✓ Safe |
| `_bespoke_play` | `if (this.module._bespoke_play)` | ✓ Safe |
| `_bespoke_stop` | `if (this.module._bespoke_stop)` | ✓ Safe |
| `_bespoke_render` | `if (this.module?._bespoke_render)` | ✓ Safe |

### Missing API Functions
The following C++ APIs are declared but never called from TypeScript:

| C++ Function | Declared In | Used in TS? | Purpose |
|--------------|-------------|-------------|---------|
| `bespoke_process_audio()` | WasmBridge.h | ❌ No | Manual audio processing (unused with SDL callback) |
| `bespoke_set_sample_rate()` | WasmBridge.h | ❌ No | Change sample rate |
| `bespoke_set_buffer_size()` | WasmBridge.h | ❌ No | Change buffer size |
| `bespoke_get_sample_rate()` | WasmBridge.h | ✅ Yes | Displayed in status |
| `bespoke_get_buffer_size()` | WasmBridge.h | ✅ Yes | Displayed in status |
| `bespoke_create_module()` | WasmBridge.h | ❌ No | Future module creation |
| `bespoke_delete_module()` | WasmBridge.h | ❌ No | Future module deletion |
| `bespoke_connect_modules()` | WasmBridge.h | ❌ No | Future module wiring |

### Thread Safety Analysis

**✓ GOOD:** SDL2AudioBackend uses atomic flags:
```cpp
std::atomic<bool> mIsRunning{false};
std::atomic<float> mOutputLevel{0.0f};
std::atomic<float> mInputLevel{0.0f};
```

**✓ GOOD:** Audio callback marks activity atomically:
```cpp
static void audioCallback(...) {
    gAudioCallbackActive.store(true);  // Atomic store
    // ... processing
}
```

**✓ GOOD:** Shutdown waits for callback completion:
```cpp
for (int i = 0; i < 10 && gAudioCallbackActive.load(); ++i) {
    // Short busy wait for callback to finish
}
```

---

## Fix Recommendations

### Priority 1: Fix Compilation Errors
**File:** `wasm/src/WasmBridge.cpp`
**Location:** After line 64

```cpp
// Add these declarations:
static InitState gInitState = InitState::NotStarted;
static std::string gInitErrorMessage;
static std::atomic<bool> gAudioCallbackActive{false};
```

### Priority 2: Remove Audio Auto-Start
**File:** `wasm/src/WasmBridge.cpp`
**Location:** Lines 265-274

Remove or comment out:
```cpp
// REMOVED: Audio should only start on user interaction
// printf("WasmBridge: Starting audio playback...\n");
// if (!gAudioBackend->start()) { ... }
```

### Priority 3: Fix State Mapping
**File:** `src/index.ts`
**Location:** Lines 243-264

```typescript
const stateToStep: Record<number, string> = {
  0: 'wasm_load',        // NotStarted → maps to wasm_load
  1: 'webgpu_instance',  // WebGPURequested
  2: 'webgpu_adapter',   // WebGPUReady
  3: 'renderer_pipelines', // RendererReady
  4: 'audio_init',       // AudioReady
  5: 'controls_create',  // FullyInitialized
};
```

### Priority 4: Use Parameters
**File:** `wasm/src/WasmBridge.cpp`
**Location:** Line 246

```cpp
// Change:
if (!gAudioBackend->initialize(44100, 512, 2, 0))
// To:
if (!gAudioBackend->initialize(sampleRate, bufferSize, 2, 0))
```

### Priority 5: Add Audio Context Resume
**File:** `src/index.ts`
**Location:** After line 442

```typescript
if (playBtn) {
  playBtn.addEventListener('click', async () => {
    // Try to resume AudioContext if suspended
    if ((window as any).Module?.SDL2?.audioContext?.state === 'suspended') {
      await (window as any).Module.SDL2.audioContext.resume();
    }
    if (this.module._bespoke_play) {
      this.module._bespoke_play();
    }
  });
}
```

---

## Summary

| Issue | Severity | Status | Fix Required |
|-------|----------|--------|--------------|
| Missing global declarations | **CRITICAL** | Build Failure | Add 3 declarations |
| Audio auto-start | **CRITICAL** | Runtime Failure | Remove auto-start code |
| State mapping mismatch | MEDIUM | UI Bug | Add state 0 mapping |
| Hardcoded parameters | LOW | Code Quality | Use function parameters |
| Missing AudioContext resume | MEDIUM | Potential Bug | Add resume handling |

**Estimated Fix Time:** 15-30 minutes

**Build Verification:** After fixes, run `./wasm/build.sh` to verify compilation.
