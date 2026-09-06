# WebGL2 Fallback Renderer

BespokeSynth WASM's production renderer remains **WebGPU/WGSL**. The WebGL2 backend is an opt-in reference path for visual debugging, Playwright screenshots, and safer iteration on UI visuals when WebGPU output is hard to inspect.

## Selecting a Renderer

Use URL flags:

```text
?renderer=webgpu
?renderer=webgl
?webgl
?webgpu
```

The header **Renderer** dropdown persists the choice in `localStorage` (`bespokesynth.renderer`) and reloads with the matching `?renderer=` parameter.

Automation can read:

```js
window.rendererType       // 'webgpu' | 'webgl'
window.usingWebGPU
window.usingWebGL
window.rendererFallbackReason
window.__bespoke.captureScreenshot()
window.__bespoke.getRendererBackend()
```

Set the backend **before** `bespoke_init()` from TypeScript:

```js
Module._bespoke_set_renderer_backend(1); // 0 = WebGPU, 1 = WebGL2
```

## Shared Patch State

Both backends render the same `ModuleCanvas` graph:

- Module positions, ports, and connections
- Transport / scale singleton modules
- Demo panel knobs and legacy view mode
- Runtime theme overrides via `bespoke_set_theme_color()`

All canvas code programs against the shared `Renderer2D` interface (`wasm/include/BespokeWasm/Renderer2D.h`).

## WebGL2 Debug Modes

Available when `?renderer=webgl` (header **Debug** dropdown or `bespoke_set_webgl_debug_mode()`):

| Mode | Value | Effect |
|------|-------|--------|
| Normal | 0 | Default visual parity path |
| Wireframe | 1 | Thin stroke lines for paths |
| Connection debug | 2 | Highlights cable endpoints |
| Simplified modules | 3 | Flat rects instead of rounded panels |

## Screenshots

### WebGL2 (recommended for CI/agents)

When WebGL2 is explicitly requested (`?renderer=webgl` / render-test), the shell enables `preserveDrawingBuffer` via `bespoke_set_webgl_preserve_drawing_buffer(1)` so standard canvas readback works:

```js
await window.__bespoke.captureScreenshot();
// Header button or Ctrl+Shift+S
```

C++ triggers a fresh frame via `bespoke_capture_screenshot()` (returns `1` for WebGL2).

### WebGPU

`bespoke_capture_screenshot()` copies the swap-chain texture to CPU memory (BGRA→RGBA) and returns `0`. Pixels are read with:

```js
const wPtr = Module._malloc(4), hPtr = Module._malloc(4);
const code = Module._bespoke_capture_screenshot(wPtr, hPtr);
const lenPtr = Module._malloc(4);
const ptr = Module._bespoke_get_screenshot_pixels(lenPtr);
// HEAPU8.subarray(ptr, ptr + HEAP32[lenPtr >> 2])
```

TypeScript wraps both paths in `captureCanvasScreenshot()` (`src/rendererMode.ts`).

### Cropping

```js
await window.__bespoke.captureScreenshot({ x: 0, y: 40, width: 800, height: 500 });
```

### Callbacks

```js
window.__bespoke.onScreenshotCaptured((dataUrl, { width, height, backend }) => {
  // e.g. post to an agent or CI artifact store
});
```

## Render test mode

Canonical scene for PNG diffing:

```text
?renderTest=1
?renderer=webgl&renderTest=1
```

```js
Module._bespoke_set_render_test_mode(1);
Module._bespoke_get_render_test_mode(); // 1 when active
```

Opens `wasm/render_test.html` for quick links. Modules placed at fixed positions with known control values and cable colors.

## Pixel Font / Label Fixes

Both backends share `PixelFont.cpp` and `pixel_font_glyphs.inc`:

- ASCII 32–126 coverage with corrected lowercase glyphs (no shuffled/copied uppercase patterns)
- Extended glyphs: musical sharp/flat (UTF-8 U+266F/U+266D) and arrow symbols
- No forced uppercasing in `text()`
- Per-glyph `textWidth()` with narrower space advance and improved baseline metrics
- WGSL reference shader kept in sync via `npm run sync:font`

Visual regression overlay:

```js
Module._bespoke_set_font_test_visible(1); // toggle with 0 to hide
```

## Porting WGSL Effects to GLSL

WebGL2 uses GLSL ES 3.0 fragment shaders ported from the WebGPU/WGSL set. Sources live in:

- `wasm/src/WebGL2Shaders.cpp` (embedded at runtime)
- `wasm/shaders/render2d_gl2*.frag` (reference copies)

Pipelines wired into `WebGL2Renderer` mirror the WebGPU `drawKnob`, `drawSlider`, `drawCableWithSag`, `drawButton`, `drawToggle`, `drawVUMeter`, `drawPanel`, `drawLED`, `drawSpectrum`, `drawProgressBar`, `drawFader`, `drawModWheel`, `drawADSR`, and `text()` helpers. Animated shaders receive `uTime` from the frame clock.

1. Prototype in `wasm/src/WebGL2Renderer.cpp` when Playwright or manual screenshots need visible pixels.
2. Keep uniform/state names aligned with the WebGPU equivalents in `WebGPURenderer.cpp`.
3. Port final logic to the embedded WGSL in `WebGPURenderer.cpp` and `wasm/shaders/render2d.wgsl`.
4. Smoke both `?renderer=webgl` and `?renderer=webgpu` when the environment supports WebGPU.

Important differences:

- WebGPU uses specialized WGSL fragment shaders for knobs, sliders, and spectrum views; WebGL2 uses simplified solid-color geometry plus a subset of GLSL effects.
- WebGPU remains the source of truth for deployment-quality output.
- WebGL2 enables `preserveDrawingBuffer` only for explicit WebGL/screenshot flows; fallback WebGL2 keeps it off.

## Build Notes

WebGL2 is enabled via Emscripten link flags in `wasm/CMakeLists.txt`:

```cmake
-sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
```

Both backends compile into the same WASM binary; selection happens at runtime.

## Emscripten + HTML5 Canvas Requirements

The WebGL2 path uses Emscripten's HTML5 WebGL API (`emscripten_webgl_create_context`, `emscripten_webgl_make_context_current`, `emscripten_webgl_destroy_context`). The canvas and page must satisfy:

| Requirement | WebGPU (default) | WebGL2 (`?renderer=webgl`) |
|-------------|------------------|----------------------------|
| Canvas element | `<canvas id="canvas">` must exist before `bespoke_init()` | Same |
| Context ownership | WebGPU surface binds to `#canvas` via `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector` | Emscripten creates a WebGL2 context on the same element |
| Mutual exclusion | Do not call `canvas.getContext('webgl2')` before init when using WebGPU | Do not initialize WebGPU on the same canvas when WebGL2 is selected |
| `preserveDrawingBuffer` | Not required | Enabled in `WebGL2Context` for `canvas.toDataURL()` screenshots |
| COOP/COEP headers | Required for SharedArrayBuffer (webpack dev server sets these) | Same — WebGL2 works under cross-origin isolation |
| Emscripten module `canvas` | Pass `canvas` in the `createBespokeSynth({ canvas })` factory config | Same |

Probe support before init (from TypeScript or the shell):

```js
Module._bespoke_set_renderer_backend(1); // optional, before probe
const ok = Module._bespoke_is_webgl2_supported() === 1;
if (!ok) {
  const reason = Module.UTF8ToString(Module._bespoke_get_webgl2_error());
  console.warn('WebGL2 unavailable:', reason);
}
```

Init state values exposed by `bespoke_get_init_state()`:

| Value | State | Backend |
|-------|-------|---------|
| 0 | NotStarted | — |
| 1 | WebGPURequested | WebGPU |
| 2 | WebGPUReady | WebGPU |
| 6 | WebGL2Requested | WebGL2 |
| 7 | WebGL2Ready | WebGL2 |
| 3 | RendererReady | Both |
| 4 | AudioReady | Both |
| 5 | FullyInitialized | Both |
| -1 | Failed | Both |

Progress callbacks (`window.__bespoke_on_init_progress(step, detail)`) emit backend-specific steps such as `webgpu_requested` / `webgl_requested` so the UI can show the correct path.

### Fallback behaviour

When WebGPU init fails and the user did not force `?renderer=webgpu`, `src/index.ts` shuts down and retries with WebGL2 automatically. If WebGL2 is also unavailable, `bespoke_get_webgl2_error()` returns a human-readable reason (missing canvas, blocked WebGL, or canvas already bound to another API).
