# GPU context creation

How the WASM build sets up its WebGPU and WebGL2 rendering contexts, and the invariants later GPU work (compute analyzers, SDF text, MSAA, timestamp-based frame timing) depends on. See [webgl-fallback.md](webgl-fallback.md) for renderer selection and screenshots.

## WebGPU adapter/device options (`wasm/src/WebGPUContext.cpp`)

- **Power preference**: `WGPURequestAdapterOptions.powerPreference = WGPUPowerPreference_HighPerformance`. The UI redraws every frame regardless of audio activity, so on multi-GPU (laptop) systems we ask for the discrete GPU rather than the browser's low-power default.
- **Required limits**: default limits, plus one raised field. Everything the renderer and analyzers create fits the WebGPU defaults (the largest buffer is the screenshot staging buffer; the analyzer FFT is one 256-invocation workgroup with 4 KiB of workgroup memory and 2 KiB of storage buffers), so `requiredLimits` is *not* a copy of the adapter's limits — a full copy can make `requestDevice` fail on multi-adapter machines. The one field that scales with the user's hardware is `maxTextureDimension2D`: the surface is sized in physical pixels (CSS size × `devicePixelRatio`), so it is raised to the adapter's value when that exceeds the default 8192. Raise another field here only together with the code that needs it.
- **Required features**: none. `TimestampQuery` and `Float32Filterable` used to be requested "for later", but nothing creates a query set or samples a float32 texture, so they only changed which devices the app asked for. A feature should be added to `requiredFeatures` in the same change that uses it — e.g. `WGPUFeatureName_TimestampQuery` for a Profile/debug GPU frame-time HUD, enabled only when that HUD is switched on and the adapter reports it via `wgpuAdapterHasFeature()`.
- **Device-lost callback**: `WGPUDeviceDescriptor.deviceLostCallbackInfo` is populated. A lost device (GPU driver reset, browser-initiated reclaim) sets a `WebGPUContext::isDeviceLost()` flag; `bespoke_render()` checks it and stops issuing commands into the dead device. The C++ side calls `window.__bespoke_on_device_lost()` (see `wasm/src/WasmBridge.cpp`), which `BespokeSynthApp.handleDeviceLost()` (`src/app/BespokeSynthApp.ts`) turns into a "Graphics Device Lost" status screen with a reload button, instead of a silently black canvas. A device destroyed by our own teardown (`WGPUDeviceLostReason_Destroyed`, e.g. during `bespoke_shutdown()`) is treated as expected and ignored.
- **Uncaptured error callback**: unchanged — still logs validation/OOM/internal errors to the console via `deviceErrorCallback`.

## Surface configuration

- **Usage**: `RenderAttachment` is always requested; `CopySrc` is added only when `wgpuSurfaceGetCapabilities()` reports the surface supports it (checked once in `onDeviceReady()` and cached as `mSupportsCopySrc`). `CopySrc` is what `bespoke_capture_screenshot()` needs to copy the swap-chain texture into a staging buffer for readback — without it, screenshot captures on WebGPU were issuing a copy the surface texture was never declared to support.
- **Suboptimal handling**: when `wgpuSurfaceGetCurrentTexture()` reports `SuccessSuboptimal` (canvas size or format drifted from the configured surface — a resize or DPR change the app hasn't reconfigured for yet), `WebGPUContext` still presents that frame, then reconfigures against the current CSS canvas size at the start of the *next* `beginFrame()`, per the WebGPU spec's guidance for suboptimal surfaces.
- **Present mode / alpha mode**: unchanged (`Fifo`, `Auto`).

## HiDPI / devicePixelRatio

The canvas backing store is sized in **physical** (device) pixels; all layout, hit-testing, and mouse-event coordinates elsewhere in the app stay in **logical** (CSS) pixels — the standard HiDPI canvas technique.

- `BespokeSynthApp.resizeCanvas()` (`src/app/BespokeSynthApp.ts`) sets `canvas.width/height = Math.round(cssSize * devicePixelRatio)`. `#canvas` is styled `width/height: 100%`, so this attribute change does not affect the on-page display size — only the resolution of the buffer the browser scales into that box.
- `bespoke_init()` / `bespoke_resize()` are still called with the **logical** CSS size (`container.clientWidth/clientHeight`), matching `MouseEvent.offsetX/offsetY` (also CSS-pixel-relative) and every layout computation in `ModuleCanvas`. Nothing in the C++ hit-testing/layout code needed to change.
- `WebGPUContext::resize()` and `WebGL2Context::resize()` independently multiply the logical width/height they're given by `emscripten_get_device_pixel_ratio()` to size the actual surface/viewport — this is a private implementation detail of each context, not part of the bridge API.
- The renderer's per-frame `beginFrame(width, height, pixelRatio, time)` still receives the **logical** size (so the projection/vertex math is unchanged) plus the real `pixelRatio` (previously hardcoded to `1.0f`). `WebGL2Renderer` uses it to convert scissor rects — the one place a raw framebuffer-pixel GL call (`glScissor`) needs physical coordinates while everything else in that class works in logical pixels.
- Screenshot output size follows whichever buffer was actually captured: WebGPU screenshots report the physical capture size (`WebGPUContext::mWidth/mHeight`); WebGL2 screenshots use `canvas.toDataURL()`, whose size is `canvas.width/height` (also physical). Existing e2e/CI screenshots are unaffected since headless Chrome defaults to `devicePixelRatio = 1`.

## WebGL2 context creation (`wasm/src/WebGL2Context.cpp`)

- The WebGL2 init path no longer pre-probes support before creating the real context. `initialize()` now does a single context creation pass, and `initializeWebGL2Renderer()` in `WasmBridge.cpp` calls it directly. This avoids the previous two-context pattern that could fail in constrained/headless setups.
- `preserveDrawingBuffer` is configurable via `bespoke_set_webgl_preserve_drawing_buffer(int)` before `bespoke_init()`. The TypeScript shell enables it only for explicit `?renderer=webgl`/render-test screenshot flows; fallback-to-WebGL2 keeps it off to avoid the extra memory/bandwidth cost in normal runtime rendering.
- WebGL2 attributes are now explicit: `antialias = false` (to match current non-MSAA WebGPU visuals), `powerPreference = high-performance`, and `failIfMajorPerformanceCaveat = false` for broader fallback compatibility.
- `resize()` scales the logical width/height it receives by `devicePixelRatio` before calling `glViewport()`, matching the physical canvas backing store the JS shell sets up.

## Binary size

Recorded from a Release build (`wasm/build.sh Release`) on Emscripten 6.0.3; see the PR description for the before/after this change produced. Check current numbers with:

```sh
stat -c '%n %s bytes' wasm/dist/BespokeSynthWASM.wasm wasm/dist/BespokeSynthWASM.js
```
