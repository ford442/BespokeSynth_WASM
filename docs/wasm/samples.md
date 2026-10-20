# Samples, looper, and export

The WASM host can import audio, play it from a Sampler, record a transport-quantized loop, and bounce the compiled graph faster than realtime.

## Import

- Drag a WAV/FLAC/MP3 onto the canvas, or use **Sample**.
- Decode happens in C++ (`dr_wav` / `dr_flac` / `dr_mp3`) on the UI thread, then each channel is Catmull-Rom resampled to the device rate.
- PCM lives in `SampleStore` as an immutable `SampleBuffer`: planar float, `channelCount()` 1 or 2 (`channelData(ch)`). Mono and stereo files keep their channels; wider files are downmixed to mono. Each content hash keeps one store id for the session. A sampler's params hold a `SampleHandle` (id + generation), and the engine resolves it to a `const SampleBuffer*` when it adopts a snapshot (`rebindParams`).
- Original file bytes are stored in OPFS keyed by SHA-256. The bundled drum kit (below) is the exception: its files ship in the resource pack and are found by the same hash. Patch JSON stores `extras.sampleHash`, not PCM. The hash covers the original file bytes, so the content-addressed format is unchanged by the stereo PCM layout (previously stored samples decode as stereo on next load).

## Memory budget and eviction

OPFS is the source of truth; the heap is a cache. `SampleStore` and `LooperArenaPool` share one budget (256 MB by default, `bespoke_set_resource_budget_mb`; `residentBytes` / `resourceBudget` in `__bespoke.getEngineStats()`).

- **Pinning.** Every publish pins the samples and looper arenas the new snapshot names (`collectSnapshotResources`, from each adapter's `collectResources`) and unpins the rest as of that snapshot's generation. A freshly decoded sample stays pinned until a later snapshot leaves it out.
- **Deferred reclamation.** After a publish the bridge calls `collectUnusedResources(generation)`: while the budget is exceeded it frees the least recently referenced unpinned buffer that was unpinned at or before the snapshot the engine will process next. A buffer is never freed while a snapshot that could still be processed names it. Deleting a sampler or looper therefore frees nothing at once; its buffers become candidates.
- **Generations.** Reloading an evicted hash reuses its id under a new generation, so a stale `SampleHandle` resolves to no buffer (silence) instead of to other PCM.
- **Undo after eviction.** Restoring a sampler whose sample was evicted looks it up by hash. `findSampleOrBundled` records the miss (`SampleStore::noteMissing`), the bridge calls `window.__bespoke_on_samples_missing`, and the host reads the hashes (`bespoke_take_missing_samples`), reloads them from OPFS and calls `bespoke_samples_restored`, which republishes. A looper's arena is reallocated empty.
- **Worklet engine.** The engine instance adopts each sample under the UI's id and generation (`bespoke_audio_adopt_sample`) and drops it when the UI evicts it (`bespoke_audio_evict_sample`, relayed after the next snapshot post). Its looper arenas follow its own snapshots.

## Bundled drum kit

Four one-shots (kick, snare, hat, tom; 16-bit mono WAV, ~100 KB together) ship in `wasm/resource-pack/kits/basic/`. They are synthesized by `scripts/gen_drum_kit.py` (pure Python, fixed seed, so regenerating is bit-identical and the kit carries no third-party licence). A sampler reaches one through `extras.sampleHash` like any user sample: when `findSampleOrBundled` misses the store it decodes the matching kit file at the current device rate, once, on first use. A patch that never uses the kit decodes nothing. `?patch=drumkit` loads `savestate/wasm-drumkit.bsk` (transport → drumseq → four samplers rooted on the drum lanes' pitches → mixer → compressor → output). `?renderTest=2` stays the synth drum bus its e2e test measures. If you regenerate the kit, update the hashes in `wasm/src/BundledKit.cpp`; `test_bundled_drum_kit` fails when they drift.

## Sampler

Note-triggered, 8-voice, allocation-free callback. Modes: one-shot, loop, gate. A stereo file plays stereo (the module's output is 2 channels); a mono file stays a mono source. Waveform is a min/max peak cache over both channels, computed at load.

## Input module

`input` puts the mic bus on the graph as a regular Audio source (mono; `gain` 0–2, `mute`), so a mic can run through a filter, EQ or reverb to the output, or into a looper, with ordinary cables. It sits on the same `InputAudioBus` as below and shows the level once the header **Mic** button has started capture (`bespoke_set_input_capture_active`). It outputs only where it is cabled: Input → Filter → Output is audible, an Input with no cable is not, and monitoring through speakers (and the feedback that can come with it) is a cable you draw on purpose.

## Looper

Record / overdub / play, length quantized to bars from transport BPM. The record arena is preallocated on the UI thread (`LooperArenaPool`). Live input arrives through `bespoke_push_input_audio` (header **Mic** button): `src/audio/inputCapture.ts` runs `src/audio/processors/micCapture.worklet.js` as an AudioWorklet, which batches 512-frame mono chunks and posts them to the main thread, which pushes them into `InputAudioBus`. No SharedArrayBuffer is needed, so it works without cross-origin isolation. With the worklet engine (the cross-origin-isolated default) the capture worklet writes samples straight into the engine's shared input ring instead, and the main thread never sees them.

The arena is planar stereo (2 × 16 s at 48 kHz, ~6 MB per looper). A loop records at the width of its audio input (the mic bus is mono); overdubbing a stereo input onto a mono loop widens it by copying L to R first.

## Offline render

`bespoke_render_offline(seconds, sampleRate)` runs the same `processBlock` plan with no device and writes a WAV (float32 default, 16- or 24-bit optional). **Export WAV** downloads it. Two renders of the same graph are bit-identical (see `test_sample_io_and_offline`).

## Share

**Share** writes `#p=` plus deflated base64url patch JSON. Opening that URL in a clean profile reloads the modules. Patches that reference samples also need those hashes in OPFS, or a `.bspk` bundle (`src/samples/bspkBundle.ts`).
