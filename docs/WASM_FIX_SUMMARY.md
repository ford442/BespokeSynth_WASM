# WASM Initialization Timeout Fix

## Problem Description
The BespokeSynth WASM module was experiencing initialization timeouts when loaded from the hosted URLs (`https://test.1ink.us/modular/wasm/`). The console logs showed:

```
Initializing BespokeSynth WASM...
loadWasmModule: script loaded
loadWasmModule: factory type = function
loadWasmModule: invoking factory to create module instance
loadWasmModule: factory resolved an instance
WASM module loaded successfully
BespokeSynth WASM: Initializing (2394x1617, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization (selector=#canvas)
WebGPUContext: initializeAsync started with selector=#canvas
Setting __bespoke_on_init_complete to receive async init completion
[10 second timeout]
Failed to initialize BespokeSynth: Error: Initialization timed out waiting for WASM to complete
```

The issue was that the WebGPU async callbacks were never being fired, causing the initialization to timeout.

## Root Cause

WebGPU uses asynchronous callbacks for initialization:
1. Request GPU adapter → `onAdapterRequest` callback
2. Request device from adapter → `onDeviceRequest` callback
3. Complete initialization → call `__bespoke_on_init_complete(0)`

The problem: With Emscripten's `MODULARIZE=1` mode and `WGPUCallbackMode_AllowProcessEvents`, the WebGPU callbacks require explicit event processing via `wgpuInstanceProcessEvents()`. Without this, the callbacks never fire.

## Solution

The fix involves three key changes:

### 1. Add Event Processing Method (C++ Side)

**File**: `wasm/src/WebGPUContext.cpp`, `wasm/include/WebGPUContext.h`

Added a `processEvents()` method to explicitly process pending WebGPU events:

```cpp
void WebGPUContext::processEvents() {
    if (mInstance) {
        wgpuInstanceProcessEvents(mInstance);
    }
}
```

This method is called:
- After requesting the adapter in `initializeAsync()`
- During the render loop (before full initialization)

### 2. Export Event Processing to JavaScript

**File**: `wasm/src/WasmBridge.cpp`

Added a new exported function that JavaScript can call:

```cpp
EMSCRIPTEN_KEEPALIVE void bespoke_process_events(void) {
    // Process pending WebGPU events to allow async callbacks to fire
    if (gContext) {
        gContext->processEvents();
    }
}
```

This function is added to the CMake exported functions list:
```cmake
"-sEXPORTED_FUNCTIONS=['_main','_bespoke_init','_bespoke_process_audio','_bespoke_render','_bespoke_process_events']"
```

### 3. Poll for Events from JavaScript

**File**: `src/index.ts`

Modified the async initialization to actively poll for WebGPU events:

```typescript
// Poll for WebGPU events to ensure async callbacks are processed
const pollInterval = window.setInterval(() => {
  if (this.module._bespoke_process_events) {
    this.module._bespoke_process_events();
  }
}, 100); // Poll every 100ms
```

The timeout was also increased from 10 seconds to 30 seconds to allow more time for initialization.

### 4. Enable ASYNCIFY Support

**File**: `wasm/CMakeLists.txt`

Added ASYNCIFY support to enable async operations:

```cmake
"-sASYNCIFY=1"  # Enable async operations
"-sASYNCIFY_STACK_SIZE=65536"  # 64KB async stack
```

## How It Works

1. JavaScript calls `_bespoke_init()` → returns 1 (async pending)
2. C++ starts WebGPU adapter request via `wgpuInstanceRequestAdapter()`
3. JavaScript begins polling `_bespoke_process_events()` every 100ms
4. Each poll calls `wgpuInstanceProcessEvents()` which processes pending callbacks
5. Adapter callback fires → requests device
6. Device callback fires → completes initialization
7. C++ calls JavaScript: `window.__bespoke_on_init_complete(0)`
8. JavaScript stops polling and continues initialization

## Testing

To test these changes, you need to:

1. **Rebuild the WASM module** (requires Emscripten):
   ```bash
   cd wasm
   ./build.sh
   ```

2. **Deploy the new files**:
   - Copy `wasm/build/BespokeSynthWASM.js` to `https://test.1ink.us/modular/wasm/`
   - Copy `wasm/build/BespokeSynthWASM.wasm` to `https://test.1ink.us/modular/wasm/`

3. **Test the web app**:
   ```bash
   npm run build
   npm run dev
   ```

4. **Verify in browser console**:
   You should now see:
   ```
   WebGPUContext: initializeAsync started with selector=#canvas
   WebGPUContext: onAdapterRequest called, status=0
   WebGPUContext: Adapter found, requesting device
   WebGPUContext: onDeviceRequest called, status=0
   WebGPUContext: Device acquired, assigning to context
   WebGPUContext: onDeviceReady
   BespokeSynth WASM: Initialization complete
   WasmBridge: notifying JS of init complete (0)
   Resolving __bespoke_on_init_complete with status 0
   BespokeSynth initialized successfully (async)
   ```

## Files Modified

1. `src/index.ts` - Added event polling during async initialization, increased timeout
2. `wasm/CMakeLists.txt` - Added ASYNCIFY support, exported `bespoke_process_events`
3. `wasm/include/WebGPUContext.h` - Added `processEvents()` method declaration
4. `wasm/src/WebGPUContext.cpp` - Implemented event processing, added initial call
5. `wasm/src/WasmBridge.cpp` - Exported `bespoke_process_events()`, process events in render loop

## Additional Notes

- The polling interval of 100ms is a reasonable balance between responsiveness and CPU usage
- The 30-second timeout provides ample time for slow connections or systems
- ASYNCIFY adds some overhead but is necessary for proper async operation handling
- Event processing during the render loop ensures events continue to be processed even after init

## References

- Emscripten WebGPU Port: https://github.com/emscripten-core/emscripten/tree/main/src/library_webgpu
- WebGPU Native API: https://github.com/webgpu-native/webgpu-headers
- Emscripten ASYNCIFY: https://emscripten.org/docs/porting/asyncify.html
