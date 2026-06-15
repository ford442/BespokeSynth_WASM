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

WebGL2 mode supports standard canvas readback:

```js
const png = await window.__bespoke.captureScreenshot();
// or click the Screenshot button in the header
```

WebGPU screenshots currently use the same JS helper; full GPU texture readback may be added later.

## Pixel Font / Label Fixes

Both backends share `PixelFont.cpp`:

- ASCII 32–126 coverage with proper lowercase glyphs
- No forced uppercasing in `text()`
- Improved `textWidth()` metrics for module titles, port labels, and value readouts

## Porting WGSL Effects to GLSL

1. Prototype in `wasm/src/WebGL2Renderer.cpp` when Playwright or manual screenshots need visible pixels.
2. Keep uniform/state names aligned with the WebGPU equivalents in `WebGPURenderer.cpp`.
3. Port final logic to the embedded WGSL in `WebGPURenderer.cpp` and `wasm/shaders/render2d.wgsl`.
4. Smoke both `?renderer=webgl` and `?renderer=webgpu` when the environment supports WebGPU.

Important differences:

- WebGPU uses specialized WGSL fragment shaders for knobs, sliders, and spectrum views; WebGL2 uses simplified solid-color geometry plus a subset of GLSL effects.
- WebGPU remains the source of truth for deployment-quality output.
- WebGL2 enables `preserveDrawingBuffer` for reliable `canvas.toDataURL()` capture.

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
