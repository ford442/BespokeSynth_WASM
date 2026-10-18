# WASM Audio Architecture

## Decision

Adopt an **AudioWorklet-first** migration. The SAB-ring worklet is the **default whenever the page is cross-origin isolated**; SDL2 is the compatibility fallback (no isolation, old Safari). Expose `bespoke_process_audio_block(float* output, int frames)` as the C++ block boundary shared by SDL and the worklet transport. Do **not** enable pthread audio by default (`BESPOKE_WASM_THREADS` stays `OFF`).

## Why

The SDL callback in the default Emscripten build shares the main WASM instance with rendering, UI, and GC. Long frames risk xruns. A browser-native AudioWorklet path isolates playback scheduling from the UI thread.

## Implemented design (2026-08)

### Phase 0 — Instrumentation

`AudioHealth` (C++) records per-block:

| Field | Meaning |
| --- | --- |
| `callbackCount` | Blocks processed via `processWasmAudio` |
| `underrunCount` | Process time &gt; ~105% of buffer duration, plus worklet ring underruns |
| `lastCallbackIntervalMs` / `lastProcessTimeMs` / `maxProcessTimeMs` | Timing |
| `cpuLoad` | Smoothed process/budget ratio (`bespoke_get_cpu_load`) |
| `queueDepthFrames` / `maxQueueDepthFrames` / `capacityFrames` | SAB ring stats (worklet) |
| `backend` | `0` SDL, `1` worklet, `2` POC |

JS: `window.__bespoke.getAudioHealth()`, C: `bespoke_get_audio_health_json()`. HUD when `?debug=1`.

### Phase 1 — Opt-in POC (`?audioWorkletPoc=1`)

`src/audio/workletPoc.ts` + `src/audio/processors/tonePoc.worklet.js`:

1. Create `AudioWorkletNode` generating a continuous tone
2. Busy-wait the main thread ~100 ms
3. Assert worklet frame counters kept advancing

Isolation proof only — **not** the module graph. Also: `window.__bespoke.runAudioWorkletPoc()`.

### Phase 2 — Production transport: **Option B (SharedArrayBuffer ring)**

| Choice | Role |
| --- | --- |
| **B (selected)** | Main thread owns the single WASM graph; fills a SAB stereo ring via `bespoke_process_audio_block`; AudioWorklet consumes and plays |
| A (deferred) | Duplicate WASM instance inside the worklet + message-synced patch state — true RT isolation, higher memory/complexity |

**Threat / deploy model for B:**

- Requires **cross-origin isolation**: `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` (and typically `Cross-Origin-Resource-Policy: same-origin` on static assets / worklet scripts).
- Webpack **devServer** and `scripts/e2e-static-server.js` set these headers (the e2e server's `/no-coi/` prefix omits them to test the fallback). Production hosts must mirror them; see [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md#cross-origin-isolation-headers-audioworklet).
- Without isolation, `SharedArrayBuffer` is unavailable; the app starts on SDL2 and says why in the header and status line.
- SAB enables high-resolution timing side channels in theory; isolation headers are the browser’s mitigation boundary. Do not widen CORP to `cross-origin` unless assets are audited.
- Pthreads remain **off**; COOP/COEP here is for SAB + worklet, not an enablement of `-pthread`.

### Phase 3 — Feature flag + parity

| Mechanism | Example |
| --- | --- |
| Auto (no flag) | worklet if `crossOriginIsolated && SharedArrayBuffer && AudioWorkletNode`, else SDL2 |
| URL | `?audio=worklet` / `?audio=sdl` (override; persisted) |
| localStorage | `bespokesynth.audio` |
| Header dropdown | Audio: Worklet / SDL2 (reloads; worklet disabled when unavailable) |

### Adaptive ring lookahead

The ring's capacity is 0.5 s, but the main-thread producer only fills it up to a **lookahead target**. The target starts at ~50 ms and never drops below ~20 ms (`src/audio/lookahead.ts`):

- New worklet underruns grow the target by 1.5x and raise a floor to 1.25x the level that starved.
- 10 s without underruns lets the target decay by 12.5% toward the floor.
- Underruns while the tab is hidden (timers throttled to ~1 s) are ignored.

The prefill runs before the worklet's `running` flag is set, so the first quantum is not an underrun. Under Playwright + SwiftShader, software GL stalls the main thread for 100–200 ms. The target climbs to roughly 0.3–0.5 s there and stays finite. The graph's own `cpuLoad` is ~1% on the canonical patch.

## Compile flags

| CMake option | Default | Effect |
| --- | --- | --- |
| `BESPOKE_WASM_SIMD` | ON | `-msimd128` + `BESPOKE_WASM_SIMD=1`: `wasm/include/BespokeWasm/DspSimd.h` kernels (gain, oscillator voice mix, delay read/mix, FFT butterflies/window/magnitude). Scalar fallback when OFF. |
| `BESPOKE_WASM_LTO` | ON | `-flto` on the Release flavor. Turn OFF for faster CI iteration. |
| `INITIAL_MEMORY` | 64 MB | Grows on demand up to `MAXIMUM_MEMORY`. Peak heap is `heapBytes` in the health JSON/HUD. The canonical patch does not grow past 64 MB. |
| `MAXIMUM_MEMORY` | 2 GiB | Growth cap (browser target only). A runaway patch fails its allocation instead of exhausting the tab; it is also the largest heap whose pointers stay below 2^31, which the TS host's `ptr >> 2` view indexing relies on. |

`build.sh` forwards `BESPOKE_WASM_SIMD` / `BESPOKE_WASM_LTO` from the environment. Unit tests (`npm run test:unit`) are built with the same SIMD flag and check each kernel against a scalar reference, the delay against the old per-sample loop, and the FFT against a naive DFT.

Both backends use `AudioGraphEngine` through `processWasmAudio` / `bespoke_process_audio_block`. External mode (`bespoke_set_external_audio(1)`) makes Play/Stop toggle transport without starting SDL.

## Block processing contract

`bespoke_process_audio_block` writes interleaved stereo into WASM linear memory.

`bespoke_get_cpu_load()` — smoothed graph-process / buffer-duration ratio (not renderer time).

## Validation gates

- [x] Worklet POC continuous during 100 ms main-thread stall
- [x] Graph block API + worklet path process the real module graph behind `?audio=worklet`
- [x] CPU load / underrun stats finite while canonical patch plays
- [x] Worklet default under cross-origin isolation; SDL2 fallback with an explained reason
- [x] Canonical `?renderTest=1` patch plays 30 s on the default path with finite underruns (Playwright chromium)
- [x] No default pthreads

## Related code

| Path | Role |
| --- | --- |
| `wasm/src/SDL2AudioBackend.cpp` | Fallback device I/O |
| `src/audio/audioMode.ts` | Backend resolution (URL / stored / auto) |
| `src/audio/lookahead.ts` | Adaptive producer lookahead |
| `wasm/include/BespokeWasm/DspSimd.h` | SIMD128 block kernels |
| `wasm/src/WasmAudioEngine.cpp` | Shared block processor + health hooks |
| `wasm/src/AudioHealth.cpp` | Metrics |
| `src/audio/workletRingBackend.ts` | SAB producer host |
| `src/audio/processors/ringPlayer.worklet.js` | Worklet consumer |
| `docs/wasm/audio.md` | Graph/port semantics |

## Note / pulse routing (canvas graph)

See [wasm/audio.md](wasm/audio.md). `AudioGraphEngine` routes Audio, Modulation, Note, and Pulse edges. Pulse sources (`transport`, `clockdivider`) drive `clockdivider`, `pulsetonote`, and `drumseq`.
