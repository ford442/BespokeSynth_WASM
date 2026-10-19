# WASM roadmap

Track feature work in GitHub; this page is an orientation aid, not a replacement for issue acceptance criteria.

## Closed: first roadmap (#63–#119)

The audio graph and scheduling, patch save/load and bundled demos, the stereo audio graph, the module catalog through wave 3 (input, EQ, the note processors, graph notes on the MIDI output, a bundled drum kit, placeholders for unsupported desktop types), browser MIDI (input, channel filter, CC learn, output), undo/redo, samples and OPFS, Scala tunings, the AudioWorklet SAB ring, the AudioWorklet engine (Option A: the graph runs in a DSP-only WASM instance inside the worklet, [../wasm-audio.md](../wasm-audio.md#option-a--audioworklet-engine)), the WebGPU and WebGL2 renderers, and CI are done. The domain docs are the source of truth for how each works: [audio.md](audio.md), [live-instrument.md](live-instrument.md), [samples.md](samples.md), [gpu-context.md](gpu-context.md), [../webgl-fallback.md](../webgl-fallback.md).

## Open

| Order | Milestone | Direction | Depends on |
|---:|---|---|---|
| 1 | Host-layer hygiene | One WASM compile per build; bounded heap (`-sMAXIMUM_MEMORY`); `stringToNewUTF8` and typed host bindings; WebGPU device request trimmed to what is used; mic capture on an AudioWorklet; docs brought in line with the code | Nothing |

The UI module keeps `-sENVIRONMENT=web` (no Node/worker glue). The worklet engine is a separate link, `BespokeSynthWASM_audio`, built with `-sENVIRONMENT=worklet`; widening the UI module's environment was not needed.

Out of scope for all of the above unless an issue says otherwise: pthreads (`BESPOKE_WASM_THREADS` stays OFF), `EXPORT_ES6`, juce_dsp, VST, Python.

Before opening a new milestone, confirm it preserves the WebGL2 fallback and does not silently broaden the desktop/WASM compatibility contract.
