# WebGPU Header Fix - December 2024

## Problem

The BespokeSynth WASM build was failing with:
```
RuntimeError: unreachable
    at BespokeSynthWASM.wasm:0x242d6
    at __asyncify_wrapper_294 (BespokeSynthWASM.js:1:145210)
    at Object._bespoke_init (BespokeSynthWASM.js:1:7873)
```

This was caused by using outdated WebGPU callback API that didn't match the current Emscripten headers.

## Root Cause

The WebGPU API has evolved from using direct callback function pointers to using callback info structures:

### Old API (causing unreachable error):
```cpp
wgpuInstanceRequestAdapter(instance, &options, onAdapterRequest, userdata);
wgpuAdapterRequestDevice(adapter, &desc, onDeviceRequest, userdata);
```

### New API (correct):
```cpp
WGPURequestAdapterCallbackInfo callbackInfo = {};
callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
callbackInfo.callback = onAdapterRequest;
callbackInfo.userdata1 = this;
callbackInfo.userdata2 = nullptr;

wgpuInstanceRequestAdapter(instance, &options, callbackInfo);
```

## Solution

Updated `wasm/src/WebGPUContext.cpp` to use the new callback info structure API:

1. **Changed adapter request** (line ~138):
   - Created `WGPURequestAdapterCallbackInfo` structure
   - Set `mode` to `WGPUCallbackMode_AllowProcessEvents`
   - Passed userdata through `userdata1` field

2. **Changed device request** (line ~71):
   - Created `WGPURequestDeviceCallbackInfo` structure
   - Set `mode` to `WGPUCallbackMode_AllowProcessEvents`
   - Passed userdata through `userdata1` field

3. **Added compile flag** in `wasm/CMakeLists.txt`:
   - Added `--use-port=emdawnwebgpu` to compile options
   - This makes Emscripten's WebGPU headers available during compilation

## WebGPU Headers Source

The WebGPU headers come from **Emscripten's emdawnwebgpu port**, not from an external repository:

- **Location**: `emsdk/upstream/emscripten/cache/ports/emdawnwebgpu/`
- **Include path**: `<webgpu/webgpu.h>`
- **Enabled by**: `--use-port=emdawnwebgpu` compile/link flag

These headers are automatically downloaded and cached by Emscripten when building with the WebGPU port enabled.

## Why This Matters

The old callback API was causing the WASM code to jump to an unreachable instruction when the WebGPU functions were called with the wrong number of arguments. The new callback info structure ensures:

1. **Type safety** - Compile-time checking of callback signatures
2. **Future compatibility** - Extensible through the structure
3. **Proper memory management** - userdata pointers are properly typed

## Callback Signatures

The callbacks themselves didn't change - they still use 5 parameters:

```cpp
typedef void (*WGPURequestAdapterCallback)(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void* userdata1,
    void* userdata2
) WGPU_FUNCTION_ATTRIBUTE;
```

Only the way they're passed to the request functions changed.

## Testing

The build now completes successfully:
```bash
cd wasm
./build.sh
# or manually:
source ../emsdk/emsdk_env.sh
cd build
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Output files:
- `BespokeSynthWASM.wasm` (1.1 MB)
- `BespokeSynthWASM.js` (271 KB)
- `BespokeSynthWASM.html`

## Files Modified

1. **wasm/src/WebGPUContext.cpp**
   - Updated `handleAdapterRequest()` to use `WGPURequestAdapterCallbackInfo`
   - Updated `initializeAsync()` to use `WGPURequestAdapterCallbackInfo`

2. **wasm/CMakeLists.txt**
   - Added `--use-port=emdawnwebgpu` to compile options for BespokeSynthWASM target
   - Added documentation comment about WebGPU header source

3. **wasm/include/WebGPUContext.h**
   - No changes needed (include path was already correct)

## Future Compatibility

The Emscripten WebGPU port is regularly updated. To stay compatible:

1. Keep emsdk updated: `./emsdk install latest && ./emsdk activate latest`
2. Check WebGPU API changes in release notes
3. The callback info structure pattern should remain stable

## References

- Emscripten WebGPU Port: https://github.com/emscripten-core/emscripten/tree/main/src/library_webgpu
- WebGPU Specification: https://www.w3.org/TR/webgpu/
- Dawn WebGPU Implementation: https://dawn.googlesource.com/dawn
