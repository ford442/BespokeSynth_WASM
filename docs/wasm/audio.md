# WASM audio

Detailed architecture (SDL vs AudioWorklet, block API, CPU load) lives in [../wasm-audio.md](../wasm-audio.md). This page covers the modular graph contract used by the browser canvas.

## Graph roles

`WasmModuleAdapter::audioRole()` classifies modules for `AudioGraphEngine`:

| Role | Examples | Graph behavior |
| --- | --- | --- |
| `AudioSource` | oscillator, noise, sampler | Produce audio buffers; participate in audio topology |
| `AudioProcessor` | filter, gain, delay, adsr, looper | Process incoming audio |
| `Sink` | output | Terminal audio node |
| `ModulationSource` | lfo | Emit per-sample modulation for CV ports |
| `NoteSource` | stepsequencer, pulsetonote | Emit note on/off from beat position and/or Pulse cables |
| `PulseSource` | transport | Emit clock pulses on 16th-note boundaries |

## Port types

Compatible cables require matching `PortType` on source output and destination input:

| Port | Meaning |
| --- | --- |
| `Audio` | Sample stream |
| `Modulation` | Bipolar CV (e.g. LFO → filter cutoff) |
| `Note` | Discrete note on/off (`WasmNoteEvent`: pitch, velocity, isNoteOn, channel) |
| `Pulse` | Clock/trigger (`WasmPulseEvent`: timeBeats, strength), delivered only along Pulse cables |

Notes follow **connections only**: events from a `Note` output are delivered to modules whose `Note` inputs are cabled to that source. Web MIDI still fills a global note queue for modules that have **no** note cable (legacy free-play path). An oscillator with a note cable stays silent until gated (no free-run fallback).

Pulses follow the same cable rule. Transport exposes a `Clock` Pulse output. `pulsetonote` converts Pulse to Note (pitch/gate). ClockDivider is the next Pulse module and is not wired yet.

Oscillator is 8-voice (oldest-age steal, pitch-matched note-off). Sampler still has its own allocator.

## Scale snapshot

The Scale module stays a singleton UI editor (`WasmAudioRole::None`). `AudioGraphSnapshot::musicScale` copies `{ root, mode }` at publish time. `quantizeMidiPitch` snaps note events when the engine fills `context.notes`. Chromatic mode is identity. Real `.scl`/`.kbm` is reserved and not parsed yet.

## MIDI map (patch schema 4)

Top-level patch JSON:

```json
"midi": { "note_channel": -1, "cc": [
  { "channel": -1, "cc": 7, "module_id": 3, "control": "gain", "min": 0, "max": 1 }
]}
```

- `note_channel` ≥ 0 filters `bespoke_midi_note_on/off`; `-1` is omni.
- Empty `"cc": []` is a no-op (CC does nothing).
- Schema v3 patches without `midi` migrate to CC7 → first gain, CC74 → first filter cutoff (20–20000 Hz), CC1 → first LFO depth.

CC is applied on the UI thread via `ModuleCanvas::applyMidiCc`. MIDI learn UI is out of scope.

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

## Audio backends (playback)

| Backend | How to select | Notes |
| --- | --- | --- |
| SDL2 (default) | `?audio=sdl` or omit | Compatibility path |
| Worklet + SAB ring | `?audio=worklet` | Needs COOP/COEP; real graph via `bespoke_process_audio_block` |
| Tone POC | `?audioWorkletPoc=1` | Isolation demo only |

Health: `window.__bespoke.getAudioHealth()` / `?debug=1` HUD. Full design: [../wasm-audio.md](../wasm-audio.md).

## Adding note-driven modules

Prefer a new `WasmModuleAdapter` with `WasmAudioRole::NoteSource` or `PulseSource` (or consume notes/pulses in `processAudio` via `WasmAudioProcessContext`). Do not special-case types inside `ModuleCanvas`. See [module-porting.md](module-porting.md).
