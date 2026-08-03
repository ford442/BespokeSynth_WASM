# WASM Audio Architecture

## Decision

Adopt an **AudioWorklet-first** migration. Keep SDL2 as the default compatibility backend while introducing an interleaved WASM block-processing API for the worklet bridge. Do not enable pthread audio by default.

## Why

The current SDL callback executes against the same browser/WASM instance as rendering. It is simple but is vulnerable to main-thread stalls from UI work, shader compilation, and garbage collection. The development server already provides COOP/COEP headers, but requiring cross-origin isolation and atomics for all users is not warranted before a browser-native audio path is proven.

Pthreads remain a later option for an explicitly isolated deployment after the worklet path establishes buffer ownership, underrun instrumentation, and compatibility requirements.

## Block Processing Contract

`bespoke_process_audio_block(float* output, int frames)` writes interleaved stereo samples into WASM linear memory. It uses the same `AudioGraphEngine` as SDL and is retained as the C++ processing boundary for a future worklet transport.

`bespoke_get_cpu_load()` reports a smoothed ratio of graph processing duration to audio-block duration. A value of `0.25` means the graph used approximately 25% of the available real-time budget for recent blocks; it does not include browser or renderer time.

## Worklet POC Plan

The proof route is opt-in: `?audioWorkletPoc=1`. It must create an `AudioWorkletNode`, run a continuous tone in the worklet, then busy-wait the main thread for 100 ms after audio starts. Audio must remain continuous while the browser UI is delayed.

An AudioWorklet cannot invoke the main-thread WASM instance directly. The production follow-up therefore needs one of these explicit transports:

1. Instantiate the processing WASM in the worklet and synchronize patch state through messages.
2. Use a `SharedArrayBuffer` ring buffer with a dedicated producer and consumer, requiring COOP/COEP.

The POC validates scheduling isolation only; it is not evidence that the full graph has migrated off the main thread. The SDL backend remains active until one production transport is implemented and tested for underruns.

## Pthread Decision

`BESPOKE_WASM_THREADS` stays `OFF`. Enabling it requires `-pthread`, a cross-origin-isolated deployment, atomics-aware graph ownership, and browser compatibility testing. It must be a separately selectable backend, never a silent change to the default artifact.

## Validation Gates

- Worklet POC produces continuous audio during a 100 ms main-thread stall.
- The graph block API produces non-silent output for the canonical patch.
- CPU load remains finite and reflects a positive processing ratio while transport is playing.
- A future worklet implementation records underrun count and maximum queue depth.

## Note / pulse routing (canvas graph)

`AudioGraphEngine` builds separate edge maps for Audio, Modulation, and Note cables. Pulse edges are reserved for clock consumers; the transport already advances `mBeatPosition` and fills `mPulseEvents` on whole beats.

**Note semantics (current):**

- `NoteSource` modules (e.g. `stepsequencer`) emit `WasmNoteEvent`s for the beat range covered by each audio block.
- Events route only along Note→Note connections to destination modules.
- Oscillators with a note cable gate amplitude from those events (silent until note-on).
- Web MIDI notes apply globally only to modules **without** an incoming note cable.

Canonical module/port documentation: [wasm/audio.md](wasm/audio.md).
