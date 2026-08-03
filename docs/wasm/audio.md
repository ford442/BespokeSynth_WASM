# WASM audio

Detailed architecture (SDL vs AudioWorklet, block API, CPU load) lives in [../wasm-audio.md](../wasm-audio.md). This page covers the modular graph contract used by the browser canvas.

## Graph roles

`WasmModuleAdapter::audioRole()` classifies modules for `AudioGraphEngine`:

| Role | Examples | Graph behavior |
| --- | --- | --- |
| `AudioSource` | oscillator, noise | Produce audio buffers; participate in audio topology |
| `AudioProcessor` | filter, gain, delay, adsr | Process incoming audio |
| `Sink` | output | Terminal audio node |
| `ModulationSource` | lfo | Emit per-sample modulation for CV ports |
| `NoteSource` | stepsequencer | Emit note on/off events from transport beat position |

## Port types

Compatible cables require matching `PortType` on source output and destination input:

| Port | Meaning |
| --- | --- |
| `Audio` | Sample stream |
| `Modulation` | Bipolar CV (e.g. LFO → filter cutoff) |
| `Note` | Discrete note on/off (`WasmNoteEvent`: pitch, velocity, isNoteOn) |
| `Pulse` | Clock/trigger (transport pulse clock exists; consumers still expanding) |

Notes follow **connections only**: events from a `Note` output are delivered to modules whose `Note` inputs are cabled to that source. Web MIDI still fills a global note queue for modules that have **no** note cable (legacy free-play path). An oscillator with a note cable stays silent until gated (no free-run fallback).

## Step sequencer (`stepsequencer`)

Canvas module under the **Pulse** spawn category. Adapter: `StepSequencerModuleAdapter`.

| Control | Default | Semantics |
| --- | --- | --- |
| `pattern` | `0x1111` | 16-bit mask; bit *i* = step *i* active |
| `pitch` | 60 | MIDI note for active steps |
| `gate` | 0.75 | Gate length as a fraction of one 16th-note step |
| `steps` | 16 | Pattern length (1–16) |

Timing syncs to `AudioGraphSnapshot` transport BPM / beat position (16th notes). UI is a clickable 16-step grid; pattern is serialized like any other control via `WasmPatchState`.

Canonical demo: `?renderTest=1` (sequencer → oscillator Pitch → filter → gain → output, plus LFO CV).

## Adding note-driven modules

Prefer a new `WasmModuleAdapter` with `WasmAudioRole::NoteSource` (or consume notes in `processAudio` via `WasmAudioProcessContext::notes`). Do not special-case types inside `ModuleCanvas`. See [module-porting.md](module-porting.md).
