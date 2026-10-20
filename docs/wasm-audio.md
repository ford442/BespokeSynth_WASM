# WASM Audio Architecture

## Decision

Adopt an **AudioWorklet-first** migration. Under cross-origin isolation the default is **Option A**: a second, DSP-only WASM instance (`BespokeSynthWASM_audio`) runs the audio graph inside the AudioWorklet. Option B (the main-thread graph filling a SAB ring) stays selectable as `?audio=ring` and is the automatic fallback when the engine cannot start. SDL2 is the compatibility fallback without isolation (old Safari, plain static hosts). `bespoke_process_audio_block(float* output, int frames)` stays the C++ block boundary for SDL and the ring. Do **not** enable pthread audio (the build has no pthreads; Option A uses no Asyncify or shared WASM memory).

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

With the worklet engine, `getAudioHealth()` returns the engine's counters (read from shared memory) plus `heapBytes` (UI module), `audioHeapBytes` (engine module), `snapshotCount`, `eventDropCount`, `engineError`, and `mainThreadBlocks`: blocks the UI module rendered itself, which stays `0` while the engine plays. The engine also reports `blockTimeHistogram`, `timedNotes`/`lateNotes`/`splitBlocks`, `directScriptNotes`, the real-time counters `rtAllocations`/`rtFrees`/`rtLocks` and `engineScope` (see [Timed notes](#timed-notes) and [Real-time checks](#real-time-checks)); `noteMarginMs` is the host's current live-note delay.

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
- Without isolation, `SharedArrayBuffer` is unavailable; the app starts on SDL2 and says why in the header and status line. Hosts that cannot set headers (GitHub Pages) can opt into a service worker that adds them (`BESPOKE_COI_SW=1`, see [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md#hosts-without-header-control-service-worker)).
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
- copies params verbatim, then `WasmModuleAdapter::rebindParams` replaces the only pointers in params: `SamplerParams::sample` (resolved from its `SampleHandle` in the engine's own `SampleStore`; a handle whose generation the engine doesn't hold resolves to none) and `LooperParams::arena` (the engine's own `LooperArenaPool`);
- bounds-checks every count, node index and plan input range, so a damaged snapshot is rejected and the previous graph keeps playing.

Samples are decoded once, on the main thread. Before each snapshot the host posts any samples decoded (or reloaded under a new generation) since the last one (`{id, generation, channels, frames, rate, pcm}`, transferred); the engine adopts them under the same id and generation (`SampleStore::adopt`). After the snapshot it relays the UI module's evictions (`{type: 'evictSample', id}`). Param changes don't post snapshots: they go through the events ring as `ParamChange` records (see [wasm/audio.md](wasm/audio.md#params-and-the-control-fast-path)).

### Shared memory

| Buffer | Direction | Contents |
| --- | --- | --- |
| events ring | main → worklet | 4-word records: performance notes (QWERTY, MIDI in, uncabled script notes) with their start frame, cabled Script notes, Script queue clears, param changes (`AudioEngineEventType`) |
| Script rings | Script worker → worklet | one per running Script while the engine plays: notes with start frames, written by the worker itself, and a clock block the worklet writes back each quantum (see [Script workers](#script-workers-write-to-the-engine)) |
| input ring | mic worklet → engine worklet | mono microphone samples; `micCapture.worklet.js` writes them directly, so the mic never touches the main thread |
| MIDI-out ring | worklet → main | graph notes for the selected MIDI port (packed like `bespoke_drain_graph_notes`), drained by `GraphNotePump` |
| analysis ring | worklet → main | mid (L+R)/2 output samples for the scope/spectrum |
| status block | both | ready flag, peaks (atomic max of float bits), MIDI-capture and input-active flags, and the engine's health, beat, timing counters and block-time histogram as float64 |

Notes queued in the UI module before the engine took over (a chord held before Play) are handed to it when it starts. Once per frame (and on demand from the public API) the host mirrors beat, output/input peaks and scope samples into the UI module (`bespoke_set_remote_audio_status`, `bespoke_push_analysis_samples`), so the transport display, Output meter and analyzer read the engine's audio unchanged.

### Timed notes

A note can carry a start time: `NoteMessage::timestamp`, in seconds on the AudioContext clock (0 = untimed). The worklet passes each quantum's `currentFrame` to `bespoke_audio_process(left, right, frames, blockFrame)`, and `AudioGraphEngine::processBlock(..., hostTimeSeconds)` turns timestamps into frame offsets inside the block:

- A note due in this block **splits** it: the engine renders the frames before the note, delivers it, then renders the rest (`splitBlocks`). Onsets land within ±1 sample at offsets 1–1000 across 128-frame blocks, notes on a block boundary included (`wasm/tests/test_timed_notes.cpp`, with a fake clock).
- A note due in a later block waits in a fixed-size pending list, sized with the note ring on the UI thread, so nothing allocates. A note never overtakes an earlier-queued note for the same key, so a fast on/off pair is never reordered. Other keys don't wait.
- A note whose frame was already rendered plays at the start of the block and counts as `lateNotes`. A note more than 1 s ahead (`kMaxScheduleAheadSeconds`), or with no finite time, plays at once.
- Untimed notes, and hosts with no clock (SDL2, the ring, offline bounce), play at the block start as before; the tests check that the output is bit-identical.
- A pending note-off survives a transport stop. A patch load (`resetClock`) drops pending notes.

Event records carry the low 32 bits of the note's AudioContext frame, which the engine unwraps against the next frame it renders, so a session past 2^32 frames (about 25 h at 48 kHz) keeps its timing (`test_audio_worklet` crosses the wrap).

**Live notes.** `NoteClock` (`src/audio/noteClock.ts`) stamps QWERTY and MIDI-in notes from the event's `timeStamp`. It maps that onto the AudioContext timeline with `getOutputTimestamp()`, then adds a steady delay, so notes keep the spacing they were played with instead of snapping to quantum boundaries. The delay has three parts:

1. how far `currentTime` runs ahead of what is heard (the largest gap of the last 2 s);
2. the ring margin: 3 ms, which grows when the engine reports late notes (up to 25 ms) and decays 1 ms/s;
3. how long events wait before the page handles them: the largest recent wait, capped at 15 ms, decaying 2 ms/s.

An event that waited longer than the cap (a long main-thread frame) starts one quantum after `currentTime`, the earliest the ring still reaches in time. Without the worklet engine `NoteClock` returns 0 and notes play at once. `_bespoke_midi_note_on/off(..., time)` and `__bespoke.midiNoteOn/Off(..., time)` take an AudioContext time directly.

The latency cost of keeping the spacing is the margin. On top of the device latency a live note waits up to one rendered quantum plus 3 ms on a responsive page. A page whose input events wait longer adds up to 15 ms more; headless Chromium sits at that cap (18 ms in the e2e run).

### Script workers write to the engine

While the worklet engine plays, each running Script gets its own channel (`ScriptEngineLink.openScriptChannel`, `src/audio/engineShared.ts`): a 1024-word note ring that the worker writes, and a clock block the worklet rewrites after every quantum (beat, BPM, playing, the frame at the end of the rendered audio, sample rate; seqlock). The worker then keeps time without the main thread:

- It ticks every 5 ms from that clock instead of waiting for the host's 20 ms `tick` messages, and runs `onBeat`/`schedule` callbacks up to 30 ms before their beat.
- Notes from those callbacks carry the beat's frame, so they start on the beat; `durationMs` note-offs count from it too. Notes from timers, or from a callback that ran after its beat was rendered, play at once. At equal frames a note-off goes before a note-on.
- `setControl` from an early callback is held until the beat's time, so controls stay in step with the notes.

The processor copies each Script ring into the event stream before the main events ring (`ScriptDirectNote` records; at most 32 per Script per quantum and 16 Scripts). A Script's channel closes before its stop sends `ScriptClear`, which releases the notes it held. The engine routes these notes like `bespoke_script_note`: onto the module's own Note cables, or, when it has none, played like a keyboard and reported to MIDI out by the engine. `directScriptNotes` counts both kinds. Controls, `getControl`, logs and the watchdog still go through the main thread. Without the engine (SDL2, the ring) the worker uses host ticks, and it switches when the engine starts or stops.

The worker captures `Atomics` and the typed-array constructors before user code runs, so a script that overwrites them cannot break its channel. Its two SharedArrayBuffers are all it shares: no WASM heap. Like any shared memory they can serve as a fine-grained timer, which a cross-origin isolated page already has.

### Real-time checks

- **Allocations and locks.** `AudioRtScope` marks the engine's block. Inside it, `new`/`delete` (`RtAllocGuard.cpp`) and any lock of an `RtMutex`/`RtRecursiveMutex` (the stores the UI thread shares with the engine: `SampleStore`, `ModuleCanvas`) abort in the unit tests (`BESPOKE_WASM_RT_ASSERT`). The Debug flavor counts them instead (`BESPOKE_WASM_RT_COUNT`): `rt` in the health JSON, `rtAllocations`/`rtFrees`/`rtLocks` in `getAudioHealth()`, `rt!:` in the HUD. Release builds count locks only, since allocation counting needs the Debug `operator new`. The `SampleStore` mutex stays for the UI thread: the engine reaches PCM through pointers rebound between quanta (`rebindParams`), and a sampler + looper test with split blocks runs under the lock trap to prove it.
- **Block-time histogram.** Each quantum's processing time as a fraction of its duration, in 8 buckets split at 0.1, 0.25, 0.5, 0.75, 1, 1.5 and 2 (`blockTimeHistogram`). The last three buckets are late quanta.
- **Thread check.** `engineScope` is the JavaScript global scope the engine module runs in (3 = `AudioWorkletGlobalScope`). The Debug engine aborts a block run anywhere else.

### Measured (Playwright, headless Chromium + SwiftShader, `?renderTest=1`)

| | Worklet engine (A) | Ring (B) |
| --- | --- | --- |
| Buffering ahead of the device | 128 frames (one quantum, 2.9 ms) | adaptive lookahead, 169–253 ms observed |
| Device latency (`baseLatency + outputLatency`) | 42 ms | same device |
| 30 s soak | 8868–9344 quanta, 8–15 late quanta, `mainThreadBlocks` 0, max quantum 11–21 ms | finite, lookahead grows (see above) |
| 100 ms main-thread busy loop | 27–41 of ~35 quanta rendered during the stall, 0 underruns | 0–5760 underrun frames per run (256, 640, 1152, 4480, 4608, 5760 observed) |

**Stress** (`tests/e2e/audio-engine-timing.spec.ts`, one worker): 4 saw oscillators × 8 held notes = 32 voices, 14 cycles of 1.5 s idle then a 500 ms busy loop on the main thread. Results:

- Rendering kept pace: 0.95× real time while stalled, 0.95× idle.
- xruns stayed at the machine's background rate: 5 in 7.0 s stalled, 16 in 22 s idle.
- Block-time histogram `[8436, 0, 1522, 33, 0, 24, 2, 9]`, `rtLocks` 0, `engineScope` 3.
- A Script `onBeat` kept playing through a 2 s stall (7 → 14 notes, 0 late).
- 12 QWERTY note events were all timed, 0 late, 4 split blocks.

"Late quanta" (`underrunCount`) are quanta whose processing took longer than their own duration. Headless Chromium does not give the audio thread real-time priority, so SwiftShader occasionally preempts it for 5–50 ms whether or not the main thread is busy; the stall test therefore checks that the worklet kept rendering through the stall, which a stall that reached it would prevent. It compares against an idle window of the same length just before the stall rather than real time, because with parallel Playwright workers sharing the CPU the audio thread can fall well behind real time with or without a stall (CI runs one worker).

## Compile flags

| CMake option | Default | Effect |
| --- | --- | --- |
| `BESPOKE_WASM_SIMD` | ON | `-msimd128` + `BESPOKE_WASM_SIMD=1`: `wasm/include/BespokeWasm/DspSimd.h` kernels (gain, oscillator voice mix, delay read/mix, FFT butterflies/window/magnitude). Scalar fallback when OFF. |
| `BESPOKE_WASM_LTO` | ON | `-flto` on the Release flavor. Turn OFF for faster CI iteration. |
| `INITIAL_MEMORY` | 32 MB (UI and engine modules) | Grows on demand up to `MAXIMUM_MEMORY`. Peak heap is `heapBytes` in the health JSON/HUD. The .wasm is ~1 MB and the preload bundle ~100 KB. |
| `MAXIMUM_MEMORY` | 2 GiB | Growth cap (browser target only). A runaway patch fails its allocation instead of exhausting the tab; it is also the largest heap whose pointers stay below 2^31, which the TS host's `ptr >> 2` view indexing relies on. |

`build.sh` forwards `BESPOKE_WASM_SIMD` / `BESPOKE_WASM_LTO` from the environment. Unit tests (`npm run test:unit`) are built with the same SIMD flag and check each kernel against a scalar reference, the delay against the old per-sample loop, and the FFT against a naive DFT.

SDL2 and the ring use the UI module's `AudioGraphEngine` through `processWasmAudio` / `bespoke_process_audio_block`; the worklet engine calls `bespoke_audio_process` in its own instance. External mode (`bespoke_set_external_audio(1)`) makes Play/Stop toggle transport without starting SDL (the render loop's SDL auto-start honors it too), and remote mode (`bespoke_set_audio_remote(1)`) makes the UI module render silence and forward notes and snapshots instead.

## Block processing contract

`bespoke_process_audio_block` writes interleaved stereo into WASM linear memory.

### Stop is a hard mute

While the transport is stopped, `AudioGraphEngine::processBlock` renders silence. Nothing keeps rendering, delay and reverb tails included, until Play. Sequencers reset and held MIDI-out notes get note-offs. Keys played while stopped (QWERTY, MIDI in) are not queued for later: the engine folds them into a per-key final state. The first playing block then sends one event per touched key, a note-on if the key is still held, otherwise a note-off. Ten notes played while stopped therefore never replay as a burst on Play, and a chord held through Play still sounds.

- The worklet engine is called every quantum, so it folds notes in its stopped blocks.
- SDL and the main-thread ring stop calling `processBlock` with the transport, so `queuePerformanceNote` folds each note as it arrives (`absorbQueuedNotesWhileStopped`). A long stopped session cannot fill the note ring that way.
- The note ring is SPSC: when it is full, the producer drops the newest note and counts it in `noteDropCount` (health JSON). It never moves the consumer's read index.

### Fan-in

A module takes any number of cables. `PlanStep` input lists are ranges into flat vectors owned by the plan (`audioInputSlots`, `modulationSourceSlots`, `noteScratchSlots`, `pulseScratchSlots`), and `prepareRuntimeStates` sizes the engine's audio-input scratch from `maxAudioFanIn` and its note/pulse scratch from the plan's emitter counts, so nothing is dropped and `processBlock` still doesn't allocate. A 16-input mixer is covered by `test_engine_foundation`.

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
- [x] Option A: timed notes start within ±1 sample of their frame (C++, fake clock); QWERTY/MIDI in and Script workers send start frames
- [x] Option A: 32 voices with a 500 ms main-thread stall every 2 s add no xruns over the idle rate; Script notes continue during a 2 s stall
- [x] Option A: no lock or allocation inside the engine block (unit-test trap; counted in Debug), engine scope checked
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
| `src/audio/noteClock.ts` | Start times for QWERTY / MIDI-in notes |
| `src/script/scriptSandbox.ts`, `src/script/scriptHost.ts` | Script worker; its direct channel to the engine |
| `wasm/include/BespokeWasm/AudioRtGuard.h`, `wasm/src/RtAllocGuard.cpp` | Real-time checks (allocation/lock hooks, thread scope) |
| `src/coi/coi-serviceworker.js` | Opt-in COOP/COEP service worker for header-less hosts |
| `docs/wasm/audio.md` | Graph/port semantics |

## Note / pulse routing (canvas graph)

See [wasm/audio.md](wasm/audio.md). `AudioGraphEngine` routes Audio, Modulation, Note, and Pulse edges. Pulse sources (`transport`, `clockdivider`) drive `clockdivider`, `pulsetonote`, and `drumseq`.
