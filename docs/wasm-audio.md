# WASM Audio Architecture

## Decision

Adopt an **AudioWorklet-first** migration. Under cross-origin isolation the default is **Option A**: a second, DSP-only WASM instance (`BespokeSynthWASM_audio`) runs the audio graph inside the AudioWorklet. Option B (the main-thread graph filling a SAB ring) stays selectable as `?audio=ring` and is the automatic fallback when the engine cannot start. SDL2 is the compatibility fallback without isolation (old Safari, plain static hosts). `bespoke_process_audio_block(float* output, int frames)` stays the C++ block boundary for SDL and the ring. Do **not** enable pthread audio (`BESPOKE_WASM_THREADS` stays `OFF`; Option A uses no pthreads, Asyncify or shared WASM memory).

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
| `queueDepthFrames` / `maxQueueDepthFrames` / `capacityFrames` | SAB ring stats (ring) |
| `backend` | `0` SDL (`sdl`), `1` SAB ring (`worklet-ring`), `2` POC, `3` worklet engine (`worklet-wasm`) |

JS: `window.__bespoke.getAudioHealth()`, C: `bespoke_get_audio_health_json()`. HUD when `?debug=1`.

With the worklet engine, `getAudioHealth()` returns the engine's counters (read from shared memory) plus `heapBytes` (UI module), `audioHeapBytes` (engine module), `snapshotCount`, `eventDropCount`, `engineError`, and `mainThreadBlocks`: blocks the UI module rendered itself, which stays `0` while the engine plays.

### Phase 1 — Opt-in POC (`?audioWorkletPoc=1`)

`src/audio/workletPoc.ts` + `src/audio/processors/tonePoc.worklet.js`:

1. Create `AudioWorkletNode` generating a continuous tone
2. Busy-wait the main thread ~100 ms
3. Assert worklet frame counters kept advancing

Isolation proof only — **not** the module graph. Also: `window.__bespoke.runAudioWorkletPoc()`.

### Phase 2 — Production transport

| Choice | Role |
| --- | --- |
| **A (default, implemented 2026-10)** | Duplicate DSP-only WASM instance inside the worklet + snapshot-synced patch state. True RT isolation from the main thread. See [Option A](#option-a--audioworklet-engine) |
| B (`?audio=ring`, fallback) | Main thread owns the single WASM graph; fills a SAB stereo ring via `bespoke_process_audio_block`; AudioWorklet consumes and plays |

**Threat / deploy model (A and B):**

- Requires **cross-origin isolation**: `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` (and typically `Cross-Origin-Resource-Policy: same-origin` on static assets / worklet scripts).
- Webpack **devServer** and `scripts/e2e-static-server.js` set these headers (the e2e server's `/no-coi/` prefix omits them to test the fallback). Production hosts must mirror them; see [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md#cross-origin-isolation-headers-audioworklet).
- Without isolation, `SharedArrayBuffer` is unavailable; the app starts on SDL2 and says why in the header and status line.
- SAB enables high-resolution timing side channels in theory; isolation headers are the browser’s mitigation boundary. Do not widen CORP to `cross-origin` unless assets are audited.
- Pthreads remain **off**; COOP/COEP here is for SAB + worklet, not an enablement of `-pthread`.

### Phase 3 — Feature flag + parity

| Mechanism | Example |
| --- | --- |
| Auto (no flag) | worklet engine if `crossOriginIsolated && SharedArrayBuffer && AudioWorkletNode`, else SDL2 |
| URL | `?audio=worklet` (engine), `?audio=ring` (main-thread ring; `sab` is an alias), `?audio=sdl` (override; persisted) |
| localStorage | `bespokesynth.audio` |
| Header dropdown | Audio: Worklet / Worklet ring (main-thread DSP) / SDL2 (reloads; worklet options disabled when unavailable) |

If the engine fails to start (no `BespokeSynthWASM_audio` build, snapshot ABI mismatch, worklet error), Play falls back to the ring for the session and the status line says why.

### Adaptive ring lookahead

The ring's capacity is 0.5 s, but the main-thread producer only fills it up to a **lookahead target**. The target starts at ~50 ms and never drops below ~20 ms (`src/audio/lookahead.ts`):

- New worklet underruns grow the target by 1.5x and raise a floor to 1.25x the level that starved.
- 10 s without underruns lets the target decay by 12.5% toward the floor.
- Underruns while the tab is hidden (timers throttled to ~1 s) are ignored.

The prefill runs before the worklet's `running` flag is set, so the first quantum is not an underrun. Under Playwright + SwiftShader, software GL stalls the main thread for 100–200 ms. The target climbs to roughly 0.3–0.5 s there and stays finite. The graph's own `cpuLoad` is ~1% on the canonical patch.

The worklet engine needs none of this: it renders each 128-frame quantum on demand, so its only buffering is the device's (`AudioContext.baseLatency + outputLatency`).

## Option A — AudioWorklet engine

### Two modules

| Module | Built from | Runs in | Owns |
| --- | --- | --- | --- |
| `BespokeSynthWASM` | everything (`WASM_SOURCES`) | main thread | renderer (WebGPU/WebGL2), canvas, undo, patch JSON, sample decode, MIDI, QWERTY, script host; its own `AudioGraphEngine` for SDL2, the ring and offline bounce |
| `BespokeSynthWASM_audio` | `BESPOKE_WASM_AUDIO_SOURCES` (`wasm/CMakeLists.txt`) | AudioWorklet | `AudioGraphEngine`, adapters, SIMD kernels, Freeverb, `SampleStore` copies, `InputAudioBus`, looper arenas |

The audio module links no renderer, no emdawnwebgpu and no SDL2. It is built with `-sENVIRONMENT=worklet -sMODULARIZE` and `--extern-post-js=wasm/audio_worklet/engineProcessor.js`, so a single `audioWorklet.addModule('wasm/BespokeSynthWASM_audio.js')` defines both `createBespokeAudioEngine` and the `bespoke-audio-engine` processor. The worklet has no `fetch`: the host compiles the `.wasm` on the main thread (`compileWorkletEngine`, warmed up right after init) and passes the `WebAssembly.Module` in `processorOptions`; the processor's `instantiateWasm` hook instantiates it. Exports are listed in `wasm/exports_audio.txt` and checked by `npm run test:exports`.

Worklet-scope details: `AudioWorkletGlobalScope` has no `crypto` (the module links `wasm/src/AudioSynthGlobals.cpp`, a `std::random_device`-free copy of the few desktop globals the DSP needs) and, in Chrome, no `performance` (the processor polyfills `performance.now()` from `Date.now()` for the health timers). A worklet-only `ENVIRONMENT` makes emcc treat every browser version as supported, so the target pins `-sGROWABLE_ARRAYBUFFERS=0` to keep the main module's browser floor; the processor re-views the heap when it grows.

Sizes (Release, `-O3`, SIMD, LTO off; CI prints them to the job summary on every build):

| File | Bytes | gzip -9 |
| --- | ---: | ---: |
| `BespokeSynthWASM.wasm` | 1,392,121 | 495,184 |
| `BespokeSynthWASM.js` | 259,563 | 58,755 |
| `BespokeSynthWASM_audio.wasm` | 508,779 | 202,371 |
| `BespokeSynthWASM_audio.js` | 71,853 | 20,195 |

The UI module did **not** shrink (+~7 KB for the snapshot codec and remote hooks). It keeps its DSP on purpose: SDL2 (no isolation) and the ring fallback render on the main thread, and offline bounce (`renderOfflineWav`, Export WAV) renders the UI module's snapshot synchronously on the main thread, which needs no round trip to the worklet. Dropping the DSP would need a DSP-free UI build plus an engine-module fallback for non-isolated pages.

### Patch state: snapshots, not commands

Every `ModuleCanvas` publish (spawn, cable, control change, undo, transport, tempo, scale) already produces a POD `AudioGraphSnapshot` (nodes, connections, param arena, compiled process plan). With the engine active, the snapshot listener calls `notifyAudioGraphPublished()` instead of preparing the local engine; the host coalesces a burst of publishes into one microtask, encodes the current snapshot (`bespoke_encode_audio_graph`, `AudioGraphSnapshotCodec`) and transfers the bytes to the worklet. The worklet decodes, runs `prepareRuntimeStates` (between quanta, so `processBlock` still never allocates) and swaps the graph in. A patch load sends its snapshot with `reset`, which restarts the clock and runtime state like `resetAudioGraphEngineForPatch`.

The wire format (`wasm/src/AudioGraphSnapshotCodec.cpp`):

- starts with a magic and an ABI hash of the struct sizes; the processor refuses an engine whose `bespoke_audio_wire_abi()` differs from the UI module's, and the decoder rejects a mismatched snapshot;
- sends adapter **type ids** rather than registry indices (registration order is static-init order, so indices may differ between builds) and remaps them; an unknown type decodes as a silent node;
- copies params verbatim, then `WasmModuleAdapter::rebindParams` replaces the only pointers in params: `SamplerParams::sample` (resolved from `sampleId` in the engine's own `SampleStore`) and `LooperParams::arena` (the engine's own `LooperArenaPool`);
- bounds-checks every count, node index and SmallVec length, so a damaged snapshot is rejected and the previous graph keeps playing.

Samples are decoded once, on the main thread. Before each snapshot the host posts any samples decoded since the last one (`{id, channels, frames, rate, pcm}`, transferred); the engine adopts them under the same id (`SampleStore::adopt`).

### Shared memory

| Buffer | Direction | Contents |
| --- | --- | --- |
| events ring | main → worklet | 4-word records: performance notes (QWERTY, MIDI in, uncabled script notes), cabled Script notes, Script queue clears (`AudioEngineEventType`) |
| input ring | mic worklet → engine worklet | mono microphone samples; `micCapture.worklet.js` writes them directly, so the mic never touches the main thread |
| MIDI-out ring | worklet → main | graph notes for the selected MIDI port (packed like `bespoke_drain_graph_notes`), drained by `GraphNotePump` |
| analysis ring | worklet → main | mid (L+R)/2 output samples for the scope/spectrum |
| status block | both | ready flag, peaks (atomic max of float bits), MIDI-capture and input-active flags, and the engine's health/beat as float64 |

Notes queued in the UI module before the engine took over (a chord held before Play) are handed to it when it starts. Once per frame (and on demand from the public API) the host mirrors beat, output/input peaks and scope samples into the UI module (`bespoke_set_remote_audio_status`, `bespoke_push_analysis_samples`), so the transport display, Output meter and analyzer read the engine's audio unchanged.

### Measured (Playwright, headless Chromium + SwiftShader, `?renderTest=1`)

| | Worklet engine (A) | Ring (B) |
| --- | --- | --- |
| Buffering ahead of the device | 128 frames (one quantum, 2.9 ms) | adaptive lookahead, 169–253 ms observed |
| Device latency (`baseLatency + outputLatency`) | 42 ms | same device |
| 30 s soak | 8868–9344 quanta, 8–15 late quanta, `mainThreadBlocks` 0, max quantum 11–21 ms | finite, lookahead grows (see above) |
| 100 ms main-thread busy loop | 27–41 of ~35 quanta rendered during the stall, 0 underruns | 0–5760 underrun frames per run (256, 640, 1152, 4480, 4608, 5760 observed) |

"Late quanta" (`underrunCount`) are quanta whose processing took longer than their own duration. Headless Chromium does not give the audio thread real-time priority, so SwiftShader occasionally preempts it for 5–50 ms whether or not the main thread is busy; the stall test therefore checks that the worklet kept rendering through the stall, which a stall that reached it would prevent. It compares against an idle window of the same length just before the stall rather than real time, because with parallel Playwright workers sharing the CPU the audio thread can fall well behind real time with or without a stall (CI runs one worker).

## Compile flags

| CMake option | Default | Effect |
| --- | --- | --- |
| `BESPOKE_WASM_SIMD` | ON | `-msimd128` + `BESPOKE_WASM_SIMD=1`: `wasm/include/BespokeWasm/DspSimd.h` kernels (gain, oscillator voice mix, delay read/mix, FFT butterflies/window/magnitude). Scalar fallback when OFF. |
| `BESPOKE_WASM_LTO` | ON | `-flto` on the Release flavor. Turn OFF for faster CI iteration. |
| `INITIAL_MEMORY` | 64 MB | Grows on demand up to `MAXIMUM_MEMORY`. Peak heap is `heapBytes` in the health JSON/HUD. The canonical patch does not grow past 64 MB. |
| `MAXIMUM_MEMORY` | 2 GiB | Growth cap (browser target only). A runaway patch fails its allocation instead of exhausting the tab; it is also the largest heap whose pointers stay below 2^31, which the TS host's `ptr >> 2` view indexing relies on. |

`build.sh` forwards `BESPOKE_WASM_SIMD` / `BESPOKE_WASM_LTO` from the environment. Unit tests (`npm run test:unit`) are built with the same SIMD flag and check each kernel against a scalar reference, the delay against the old per-sample loop, and the FFT against a naive DFT.

SDL2 and the ring use the UI module's `AudioGraphEngine` through `processWasmAudio` / `bespoke_process_audio_block`; the worklet engine calls `bespoke_audio_process` in its own instance. External mode (`bespoke_set_external_audio(1)`) makes Play/Stop toggle transport without starting SDL (the render loop's SDL auto-start honors it too), and remote mode (`bespoke_set_audio_remote(1)`) makes the UI module render silence and forward notes and snapshots instead.

## Block processing contract

`bespoke_process_audio_block` writes interleaved stereo into WASM linear memory.

`bespoke_get_cpu_load()` — smoothed graph-process / buffer-duration ratio (not renderer time).

## Validation gates

- [x] Worklet POC continuous during 100 ms main-thread stall
- [x] Graph block API + worklet path process the real module graph behind `?audio=worklet`
- [x] CPU load / underrun stats finite while canonical patch plays
- [x] Worklet default under cross-origin isolation; SDL2 fallback with an explained reason
- [x] Canonical `?renderTest=1` patch plays 30 s on the default path with finite underruns (Playwright chromium)
- [x] Option A: under isolation `processBlock` runs only in the worklet (`backend` 3, `mainThreadBlocks` 0)
- [x] Option A: a 100 ms main-thread stall does not stop the worklet rendering (the ring underruns on the same stall)
- [x] Option A: spawn, cable, undo, QWERTY/MIDI in, MIDI out, arpeggiator/chorder, bundled drum-kit samples, Script `onBeat`, and microphone input play through the engine (e2e runs these specs on both `sdl` and `worklet`; Script note events and sample/looper rebinding are unit-tested); offline bounce uses the UI module's engine
- [x] SDL2 / non-isolated pages keep the single-instance main-thread graph
- [x] Engine `.wasm` size published separately (CI job summary)
- [x] No pthreads, Asyncify, Comlink or WAM SDK
- [x] No `ScriptProcessorNode` in repo code (mic capture is an AudioWorklet; the engine and ring are AudioWorklets). Follow-up: `bespoke_init` still opens the SDL2 device on every load, and the Emscripten SDL2 port's audio driver creates a `ScriptProcessorNode` (and its own `AudioContext`) when it does, idle unless SDL2 plays. Opening SDL2 only when it is the backend needs the pre-Play sample-store rate to come from somewhere else first (samples are resampled to the device rate at decode).

## Related code

| Path | Role |
| --- | --- |
| `wasm/src/SDL2AudioBackend.cpp` | Fallback device I/O |
| `src/audio/audioMode.ts` | Backend resolution (URL / stored / auto) |
| `src/audio/lookahead.ts` | Adaptive producer lookahead |
| `wasm/include/BespokeWasm/DspSimd.h` | SIMD128 block kernels |
| `wasm/src/WasmAudioEngine.cpp` | Shared block processor + health hooks |
| `wasm/src/AudioHealth.cpp` | Metrics |
| `src/audio/workletRingBackend.ts` | SAB producer host (Option B) |
| `src/audio/processors/ringPlayer.worklet.js` | Worklet consumer (Option B) |
| `src/audio/workletEngineBackend.ts` | Worklet engine host: compile, start, snapshot/sample posting, telemetry mirror (Option A) |
| `src/audio/engineShared.ts` | Shared-memory layouts (rings, status block) |
| `wasm/audio_worklet/engineProcessor.js` | Engine `AudioWorkletProcessor`, appended to the audio module's glue |
| `wasm/src/AudioWorkletEngine.cpp` | Engine C ABI (`bespoke_audio_*`, audio module only) |
| `wasm/src/AudioGraphSnapshotCodec.cpp` | Snapshot wire format |
| `wasm/src/AudioRemote.cpp` | UI-module side: note forwarding and publish notifications while the engine plays |
| `docs/wasm/audio.md` | Graph/port semantics |

## Note / pulse routing (canvas graph)

See [wasm/audio.md](wasm/audio.md). `AudioGraphEngine` routes Audio, Modulation, Note, and Pulse edges. Pulse sources (`transport`, `clockdivider`) drive `clockdivider`, `pulsetonote`, and `drumseq`.
