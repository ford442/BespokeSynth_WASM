# WASM audio

Detailed architecture (SDL vs AudioWorklet, block API, CPU load) lives in [../wasm-audio.md](../wasm-audio.md). This page covers the modular graph contract used by the browser canvas.

## Graph roles

`WasmModuleAdapter::audioRole()` classifies modules for `AudioGraphEngine`:

| Role | Examples | Graph behavior |
| --- | --- | --- |
| `AudioSource` | oscillator, noise, sampler | Produce audio buffers; participate in audio topology |
| `AudioProcessor` | filter, gain, delay, adsr, looper, mixer, compressor, chorus, waveshaper, reverb | Process incoming audio |
| `Sink` | output | Terminal audio node |
| `ModulationSource` | lfo | Emit per-sample modulation for CV ports |
| `NoteSource` | stepsequencer, pulsetonote, drumseq | Emit note on/off from beat position and/or Pulse cables |
| `PulseSource` | transport, clockdivider | Emit clock pulses; may also consume Pulse (clockdivider) |

## Port types

Compatible cables require matching `PortType` on source output and destination input:

| Port | Meaning |
| --- | --- |
| `Audio` | Sample stream |
| `Modulation` | Bipolar CV (e.g. LFO → filter cutoff) |
| `Note` | Discrete note on/off (`WasmNoteEvent`: pitch, velocity, isNoteOn, channel, outputPort) |
| `Pulse` | Clock/trigger (`WasmPulseEvent`: timeBeats, strength), delivered only along Pulse cables |

Notes follow **connections only**: events from a `Note` output are delivered to modules whose `Note` inputs are cabled to that source. Web MIDI still fills a global note queue for modules that have **no** note cable (legacy free-play path). An oscillator with a note cable stays silent until gated (no free-run fallback).

Pulses follow the same cable rule. Transport exposes a `Clock` Pulse output. `pulsetonote` converts Pulse to Note (pitch/gate).

### Per-port routing

- **Audio inputs.** By default the engine sums every audio cable into the module's buffer. An adapter that returns `sumsAudioInputs() == false` (mixer, compressor) gets a zeroed buffer plus `context.audioInputs[]`, one `{ buffer, port }` per cable, where `port` is the destination input index. `accumulateAudioInputPort(context, port, dst)` sums one port. Up to 8 audio cables per module.
- **Note outputs.** A source with more than one `Note` output (drumseq) tags each event with `WasmNoteEvent::outputPort` (its ordinal among the Note outputs). The plan stores a per-cable filter (`PlanStep::noteSourcePortFilters`), so a cable from drumseq *Hat* only delivers hat notes. Single-output sources are unfiltered.
- **Pulse processors.** A `PulseSource` may take Pulse inputs. The plan orders pulse sources upstream-first (`pulseSourceNodeIndices` + `pulseSourcePlans`) and the engine passes the upstream pulses of the same block to `emitPulsesForBeatRange(..., inputPulses, inputPulseCount)`. A pulse cycle is cut: a source only reads slots evaluated before it.
- **Scale.** `quantizesNotesToScale()` defaults to true; drumseq returns false because its pitches are trigger ids.

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

## Catalog wave 2 (FX / Pulse)

| Type id | Spawn category | Ports (in → out) | Controls | DSP |
| --- | --- | --- | --- | --- |
| `mixer` | FX | 4× Audio → Audio | `level1..4`, `master` | SIMD `accumulateScaled` per port. Mono bus, so no pan yet |
| `compressor` | FX | Audio In, Audio Sidechain → Audio | `threshold` dB, `ratio`, `attack`/`release` ms, `makeup` dB, `knee` dB | In-adapter feed-forward, soft knee, log-domain ballistics |
| `chorus` | FX | Audio, Mod → Audio | `rate`, `depth`, `delay` ms, `feedback`, `mix` | 2-voice modulated delay, fixed 4096-sample ring in runtime state. Mod CV adds up to ±6 ms |
| `waveshaper` | FX | Audio, Mod (drive) → Audio | `drive`, `mode` (Soft/Tanh/Fold/Hard), `mix`, `level` | Stateless per-sample. Drive CV scales drive by (1 + cv) |
| `reverb` | FX | Audio → Audio | `room`, `damping`, `mix` | `libs/freeverb` `revmodel` (see below) |
| `clockdivider` | Pulse | Pulse → Pulse | `divide` 1–32, `offset` | Passes every Nth pulse |
| `drumseq` | Pulse | Pulse → 4× Note (Kick/Snare/Hat/Tom) | `kick`/`snare`/`hat`/`tom` 16-bit masks, `*pitch`, `steps`, `gate` | Each pulse advances one step. Silent without a Pulse cable |

Sliders use the shared `SliderPanelModule` (one spec table per adapter drives controls, defaults, clamping and UI).

**Reverb / Freeverb.** The `reverb` type keeps its id and controls (saved patches load unchanged) but its kernel is now Jezar's Freeverb from `libs/freeverb` (public domain), the same code the desktop `FreeverbEffect` wraps. The desktop effect class is not compiled. `revmodel` holds pointers into its own buffers, so the adapter detects a runtime-arena move (`boundTo != this`) and rebuilds the model in place. The tail restarts; nothing dangles. It uses 44.1 kHz tunings at every rate.

**juce_dsp spike.** `juce_dsp.h` does compile for wasm (`JUCE_USE_SIMD` resolves to 0, so the SIMD `#error` never fires). It is not header-only, though: `Compressor`/`Chorus`/`DelayLine`/`WaveShaper` bodies and their explicit instantiations live in `juce_dsp.cpp`, which pulls FFT/convolution/oversampling. The compressor and chorus kernels are therefore in-adapter scalar code, and `BESPOKE_WASM_JUCE_DSP` was not added.

Second fixture: `?renderTest=2`. Transport → drumseq; Kick → sine osc (55 Hz), Snare/Hat → ADSRs on one noise source → mixer ch 1–3 → compressor → output. It plays a four-on-the-floor loop with no MIDI (`tests/e2e/drum-bus.spec.ts`).

## Audio backends (playback)

| Backend | How to select | Notes |
| --- | --- | --- |
| Worklet + SAB ring (default) | automatic when `crossOriginIsolated`, or `?audio=worklet` | Real graph via `bespoke_process_audio_block`; adaptive lookahead |
| SDL2 | `?audio=sdl`, or automatic without isolation | Compatibility path (old Safari, hosts without COOP/COEP) |
| Tone POC | `?audioWorkletPoc=1` | Isolation demo only; not in the header dropdown |

Resolution order: `?audio=` (persisted to `localStorage` `bespokesynth.audio`) → stored preference → auto. A worklet request on a page that is not cross-origin isolated falls back to SDL2; the header shows *Audio: SDL2 (fallback)* with the reason in its tooltip and a status message. `window.__bespoke.getAudioBackendChoice()` returns `{ backend, requested, source, fallbackReason? }`.

Health: `window.__bespoke.getAudioHealth()` (includes `heapBytes`), `window.__bespoke.getAudioRingStats()` (lookahead), and the `?debug=1` HUD. Full design: [../wasm-audio.md](../wasm-audio.md).

## Production headers (required for the worklet)

The worklet backend needs `SharedArrayBuffer`, which browsers only expose on cross-origin isolated pages. Serve **every** response (HTML, JS, `.wasm`, `.data`, `audio/*.worklet.js`) with:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: same-origin
```

`COEP: require-corp` blocks cross-origin subresources that do not send CORP/CORS headers. Keep assets same-origin, or have the other origin send `Cross-Origin-Resource-Policy: cross-origin`. Without these headers the app still works on SDL2. Server snippets: [../DEPLOYMENT_GUIDE.md](../DEPLOYMENT_GUIDE.md#cross-origin-isolation-headers-audioworklet). Pthreads stay off; these headers are only for SAB.

## Adding note-driven modules

Prefer a new `WasmModuleAdapter` with `WasmAudioRole::NoteSource` or `PulseSource` (or consume notes/pulses in `processAudio` via `WasmAudioProcessContext`). Do not special-case types inside `ModuleCanvas`. See [module-porting.md](module-porting.md).
