# WASM roadmap

Track feature work in GitHub; this page is an orientation aid, not a replacement for issue acceptance criteria.

| Milestone | Issue | Current direction |
|---|---:|---|
| Audio graph and scheduling | [#63](https://github.com/ford442/BespokeSynth_WASM/issues/63) | Block processing, notes, pulses, CV |
| Patch save/load and bundled demos | [#64](https://github.com/ford442/BespokeSynth_WASM/issues/64) | Portable WASM patch state |
| More module types | [#71](https://github.com/ford442/BespokeSynth_WASM/issues/71) | Port modules incrementally with tests |
| Browser MIDI | [#73](https://github.com/ford442/BespokeSynth_WASM/issues/73) | Main-thread input into audio-safe events |
| Rendering and visual diagnostics | [#79](https://github.com/ford442/BespokeSynth_WASM/issues/79) | WebGPU first, WebGL2 comparison path |

Before opening a new milestone, confirm it preserves the WebGL2 fallback and does not silently broaden the desktop/WASM compatibility contract.
