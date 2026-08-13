# WASM Audio Architecture

## Decision

Adopt an **AudioWorklet-first** migration. Keep **SDL2 as the default** compatibility backend. Expose `bespoke_process_audio_block(float* output, int frames)` as the C++ block boundary shared by SDL and the worklet transport. Do **not** enable pthread audio by default (`BESPOKE_WASM_THREADS` stays `OFF`).

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
- Webpack **devServer** and `scripts/e2e-static-server.js` set these headers. Production CDNs must mirror them to use `?audio=worklet`.
- Without isolation, `SharedArrayBuffer` is unavailable; Play falls back to SDL2.
- SAB enables high-resolution timing side channels in theory; isolation headers are the browser’s mitigation boundary. Do not widen CORP to `cross-origin` unless assets are audited.
- Pthreads remain **off**; COOP/COEP here is for SAB + worklet, not an enablement of `-pthread`.

### Phase 3 — Feature flag + parity

| Mechanism | Example |
| --- | --- |
| URL | `?audio=sdl` (default) / `?audio=worklet` |
| localStorage | `bespokesynth.audio` |
| Header dropdown | Audio: SDL2 / Worklet (reloads) |

Both backends use `AudioGraphEngine` through `processWasmAudio` / `bespoke_process_audio_block`. External mode (`bespoke_set_external_audio(1)`) makes Play/Stop toggle transport without starting SDL.

## Block processing contract

`bespoke_process_audio_block` writes interleaved stereo into WASM linear memory.

`bespoke_get_cpu_load()` — smoothed graph-process / buffer-duration ratio (not renderer time).

## Validation gates

- [x] Worklet POC continuous during 100 ms main-thread stall
- [x] Graph block API + worklet path process the real module graph behind `?audio=worklet`
- [x] CPU load / underrun stats finite while canonical patch plays
- [x] SDL2 remains default; worklet opt-in with SDL fallback
- [x] No default pthreads

## Related code

| Path | Role |
| --- | --- |
| `wasm/src/SDL2AudioBackend.cpp` | Default device I/O |
| `wasm/src/WasmAudioEngine.cpp` | Shared block processor + health hooks |
| `wasm/src/AudioHealth.cpp` | Metrics |
| `src/audio/workletRingBackend.ts` | SAB producer host |
| `src/audio/processors/ringPlayer.worklet.js` | Worklet consumer |
| `docs/wasm/audio.md` | Graph/port semantics |

## Note / pulse routing (canvas graph)

See [wasm/audio.md](wasm/audio.md). `AudioGraphEngine` routes Audio, Modulation, and Note edges; pulse clock events are reserved for future consumers.
