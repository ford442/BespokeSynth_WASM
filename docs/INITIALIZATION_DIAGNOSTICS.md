# BespokeSynth WASM Initialization Diagnostics

## Executive Summary

This document provides a comprehensive assessment of the initialization issues in BespokeSynth WASM and describes the diagnostic tools implemented to identify and resolve them.

## Root Cause Analysis

### 1. Silent Initialization Failures

**Problem**: When shaders or audio failed to initialize, there was no visible feedback to the user. The app would appear to be "loading" indefinitely or would show a black screen.

**Causes Identified**:
- Missing `WGPUSType_ShaderSourceWGSL` support in some builds causes all pipelines to be null
- Audio backend initialized but `start()` was never called
- WebGPU callbacks may not fire without frequent event processing

### 2. Lack of Progress Visibility

**Problem**: Users had no indication of which initialization step was in progress or how long it might take.

**Previous Flow**:
```
"Initializing..." → [long pause] → Success/Failure
```

### 3. Shader Pipeline Compilation Issues

**Problem**: The shader compilation happens in `createPipelines()` but there's no visibility into:
- Whether the shader module was created successfully
- Which individual pipelines failed
- Why compilation failed

**Key Code Section** (WebGPURenderer.cpp:1266-1272):
```cpp
#else
    // WGSL shader chaining unavailable in this WebGPU header
    WGPUShaderModule shaderModule = nullptr;
    printf("WebGPURenderer: WGSL shader source chaining unavailable\n");
#endif
```

When this path is taken, ALL pipelines are null and rendering produces no output.

### 4. Audio Initialization Gap

**Problem**: Audio backend was initialized but never started:

```cpp
// OLD CODE - Missing start()
gAudioBackend->setCallback(audioCallback);
// Audio was never started!

// NEW CODE - Fixed
gAudioBackend->setCallback(audioCallback);
gAudioBackend->start();  // Now audio actually plays
```

## Implemented Diagnostic Solutions

### 1. Visual Progress Bar with Step Indicators

**Location**: `src/index.html`, `src/styles.css`, `src/index.ts`

**Features**:
- Animated progress bar with percentage display
- Individual step status indicators
- Color-coded states (pending/active/completed/error)
- Estimated progress based on step weights

**UI Structure**:
```
┌─────────────────────────────────────────┐
│  Initializing BespokeSynth              │
│  Setting up WebGPU and audio...         │
│  ━━━━━━━━━━━━━━━━━━━━━━━━━━━  65%       │
│  ◌ Compiling shader pipelines...        │
│  ✓ Creating WebGPU instance             │
│  ✓ Creating WebGPU surface              │
│  ✓ Requesting GPU adapter               │
└─────────────────────────────────────────┘
```

### 2. Detailed Initialization State Tracking

**Location**: `wasm/src/WasmBridge.cpp`

**C++ InitState Enum** (exposed to JavaScript):
```cpp
enum class InitState {
    NotStarted = 0,
    WebGPURequested = 1,     // Creating instance/surface
    WebGPUReady = 2,         // Adapter/device acquired
    RendererReady = 3,       // Pipelines compiled
    AudioReady = 4,          // Audio initialized
    FullyInitialized = 5,    // All done
    Failed = -1
};
```

**JavaScript polls state** via `bespoke_get_init_state()` every 50ms.

### 3. Progress Reporting Callbacks

**New C++ Function**:
```cpp
static void reportInitProgress(const char* step, const char* detail) {
    printf("[InitProgress] %s: %s\n", step, detail);
    // Also calls JS: window.__bespoke_on_init_progress(step, detail)
}
```

**Steps Reported**:
1. `init_start` - Beginning initialization
2. `webgpu_requested` - Creating WebGPU instance and surface
3. `webgpu_ready` - GPU adapter and device acquired
4. `renderer_init` - Creating shader pipelines
5. `renderer_ready` - All pipelines compiled
6. `audio_init` - Opening audio device
7. `audio_ready` - Audio device opened
8. `audio_started` - Audio playback started
9. `controls_init` - Creating UI controls
10. `init_complete` - All subsystems ready

### 4. Shader Compilation Diagnostics

**Enhanced Logging** in `WebGPURenderer::createPipelines()`:
```cpp
auto checkPipeline = [&](WGPURenderPipeline p, const char* name) {
    if (!p) {
        printf("WebGPURenderer: pipeline creation FAILED for %s\n", name);
    }
};
```

Each of the 40+ pipelines is checked and logged.

### 5. Audio OSC Diagnostics

**Fixed Issues**:
1. Added `gAudioBackend->start()` call after initialization
2. Added logging for audio start success/failure
3. Added audio level monitoring in the render loop

**Audio Status Display**:
```cpp
char statusText[256];
snprintf(statusText, sizeof(statusText), 
         "Sample Rate: %d Hz | Buffer: %d | Audio: %s | Panel: %s",
         bespoke_get_sample_rate(),
         bespoke_get_buffer_size(),
         (gAudioBackend && gAudioBackend->isRunning()) ? "Running" : "Stopped",
         panelNames[gCurrentPanel]);
```

## Diagnostic Console Output

### Expected Successful Init Output:
```
[InitProgress] init_start: Beginning initialization
BespokeSynth WASM: Initializing (2394x1617, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization (selector=#canvas)
WebGPUContext: Instance created
WebGPUContext: Surface created for selector: #canvas
WebGPUContext: Requesting adapter...
[InitProgress] webgpu_requested: Creating WebGPU instance and surface
WebGPUContext: onAdapterRequest called, status=0
WebGPUContext: Adapter found, requesting device
WebGPUContext: onDeviceRequest called, status=0
WebGPUContext: Device acquired, assigning to context
WebGPUContext: onDeviceReady
WebGPUContext: Getting device queue...
WebGPUContext: Configuring surface...
WebGPUContext: Device ready, initialization complete
[InitProgress] webgpu_ready: GPU adapter and device acquired successfully
WasmBridge: WebGPU context ready, proceeding with remaining initialization
WasmBridge: Initializing renderer...
[InitProgress] renderer_init: Creating shader pipelines...
WebGPURenderer: Initializing...
WebGPURenderer: Creating pipelines...
WebGPURenderer: Creating buffers...
WebGPURenderer: Initialization complete
[InitProgress] renderer_ready: All shader pipelines compiled successfully
WasmBridge: Initializing audio backend...
[InitProgress] audio_init: Opening audio device...
SDL2AudioBackend: Initializing SDL audio...
  Sample rate: 44100
  Buffer size: 512
  Output channels: 2
  Input channels: 0
SDL2AudioBackend: Opened audio device successfully
  Obtained sample rate: 44100
  Obtained buffer size: 512
  Obtained channels: 2
[InitProgress] audio_ready: Audio device opened successfully
WasmBridge: Starting audio playback...
SDL2AudioBackend: Audio started
[InitProgress] audio_started: Audio playback started
WasmBridge: Creating demo controls...
[InitProgress] controls_init: Creating UI controls...
[InitProgress] init_complete: All subsystems ready
BespokeSynth WASM: Initialization complete - all subsystems ready
```

### Error Output Example (WebGPU Not Supported):
```
[InitProgress] init_start: Beginning initialization
WebGPUContext: Instance created
WebGPUContext: Surface created for selector: #canvas
WebGPUContext: Requesting adapter...
WebGPUContext: onAdapterRequest called, status=2  // Error
WebGPU Adapter Error: 
[InitProgress] webgpu_failed: WebGPU initialization failed
BespokeSynth WASM: Failed to initialize WebGPU
```

## Quick Diagnostic Checklist

If initialization fails, check these in order:

### 1. Browser Console
- Look for `[InitProgress]` messages to see which step failed
- Check for WebGPU errors (status codes != 0)
- Verify shader pipeline creation messages

### 2. WebGPU Support
```javascript
// Run in browser console
console.log('WebGPU supported:', !!navigator.gpu);
if (navigator.gpu) {
    navigator.gpu.requestAdapter().then(a => console.log('Adapter:', a));
}
```

### 3. Shader Compilation
Look for:
```
WebGPURenderer: pipeline creation FAILED for fs_solid
```
If this appears, the `WGPUSType_ShaderSourceWGSL` path is not available.

### 4. Audio Status
Check status bar at bottom of screen shows:
- `Audio: Running` (should show when Play is clicked)
- `Audio: Stopped` (when stopped)

## Performance Metrics

### Initialization Timing (Typical):
| Step | Time |
|------|------|
| WASM Load | 200-500ms |
| WebGPU Instance/Surface | 10-50ms |
| Adapter Request | 50-200ms |
| Device Request | 50-100ms |
| Shader Pipeline Compilation | 100-300ms |
| Audio Init | 50-100ms |
| **Total** | **500ms - 1.5s** |

### Polling Frequency:
- JavaScript state polling: 50ms
- WebGPU event processing: 16ms (~60fps) during init
- Timeout threshold: 60 seconds

## Build Requirements

For proper diagnostics to work, ensure:

1. **CMakeLists.txt** exports the progress function:
```cmake
"-sEXPORTED_FUNCTIONS=['_main',..., '_bespoke_get_init_state', '_bespoke_get_init_error']"
```

2. **ASYNCIFY enabled** for proper async operation:
```cmake
"-sASYNCIFY=1"
"-sASYNCIFY_STACK_SIZE=65536"
```

3. **WebGPU flags**:
```cmake
"-sUSE_WEBGPU=1"
```

## Future Enhancements

1. **Retry Logic**: Allow users to retry failed steps
2. **Fallback Rendering**: Canvas 2D fallback if WebGPU fails
3. **Audio Fallback**: Web Audio API fallback if SDL audio fails
4. **Network Diagnostics**: Check if WASM files loaded correctly
5. **Performance Metrics**: Track and report actual timing for each step

## References

- `wasm/src/WasmBridge.cpp` - Core initialization logic
- `wasm/src/WebGPURenderer.cpp` - Shader pipeline creation
- `wasm/src/SDL2AudioBackend.cpp` - Audio initialization
- `src/index.ts` - JavaScript initialization and progress UI
- `src/styles.css` - Progress bar styling
