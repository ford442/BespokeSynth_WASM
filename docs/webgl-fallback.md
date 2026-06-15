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
