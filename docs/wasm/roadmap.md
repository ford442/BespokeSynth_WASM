# WASM roadmap

Track feature work in GitHub; this page is an orientation aid, not a replacement for issue acceptance criteria.

## Closed: first roadmap (#63–#119)

The audio graph and scheduling, patch save/load and bundled demos, the module catalog through wave 2, browser MIDI (input, channel filter, CC learn, output), undo/redo, samples and OPFS, Scala tunings, the AudioWorklet SAB ring, the WebGPU and WebGL2 renderers, and CI are done. The domain docs are the source of truth for how each works: [audio.md](audio.md), [live-instrument.md](live-instrument.md), [samples.md](samples.md), [gpu-context.md](gpu-context.md), [../webgl-fallback.md](../webgl-fallback.md).

## Open

| Order | Milestone | Direction | Depends on |
|---:|---|---|---|
| 1 | Host-layer hygiene | One WASM compile per build; bounded heap (`-sMAXIMUM_MEMORY`); `stringToNewUTF8` and typed host bindings; WebGPU device request trimmed to what is used; mic capture on an AudioWorklet; docs brought in line with the code | Nothing |
| 2 | Catalog wave 3 | Stereo mixer, EQ, note processors | 1 |
| 3 | Option A: worklet-hosted DSP | Run the graph inside the AudioWorklet instead of filling the SAB ring from the main thread (Option B, today). Needs `-sENVIRONMENT` widened beyond `web` and the typed host bindings from 1 | 1 |

`-sENVIRONMENT=web` is correct for Option B and is deliberately unchanged until Option A starts: it keeps Node/worker glue out of the runtime, and it is what stops the same `.wasm` from instantiating inside an AudioWorklet.

Out of scope for all of the above unless an issue says otherwise: pthreads (`BESPOKE_WASM_THREADS` stays OFF), `EXPORT_ES6`, juce_dsp, VST, Python.

Before opening a new milestone, confirm it preserves the WebGL2 fallback and does not silently broaden the desktop/WASM compatibility contract.
