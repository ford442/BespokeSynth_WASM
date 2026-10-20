# WASM audio

Detailed architecture (SDL vs AudioWorklet, block API, CPU load) lives in [../wasm-audio.md](../wasm-audio.md). This page covers the modular graph contract used by the browser canvas.

## Graph roles

`WasmModuleAdapter::audioRole()` classifies modules for `AudioGraphEngine`:

| Role | Examples | Graph behavior |
| --- | --- | --- |
| `AudioSource` | oscillator, noise, sampler, input | Produce audio buffers; participate in audio topology |
| `AudioProcessor` | filter, gain, delay, adsr, looper, mixer, compressor, chorus, waveshaper, reverb, eq | Process incoming audio |
| `Sink` | output | Terminal audio node |
| `ModulationSource` | lfo | Emit per-sample modulation for CV ports |
| `NoteSource` | stepsequencer, pulsetonote, drumseq | Emit note on/off from beat position and/or Pulse cables |
| `NoteProcessor` | chorder, arpeggiator, notefilter, noterouter | Take Note events in and emit Note events out, in the same block (see [Note processors](#note-processors)) |
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

- **Audio inputs.** By default the engine sums every audio cable into the module's channels. An adapter that returns `sumsAudioInputs() == false` (mixer, compressor) gets zeroed channels plus `context.audioInputs[]`, one `{ channels[2], numChannels, port }` per cable, where `port` is the destination input index. `accumulateAudioInputPort(context, port, dst, numChannels)` sums one port; `audioInputPortChannels(context, port)` reports its width. There is no cable limit: plan steps hold ranges into flat input vectors owned by the plan, sized when it compiles.
- **Note outputs.** A source with more than one `Note` output (drumseq) tags each event with `WasmNoteEvent::outputPort` (its ordinal among the Note outputs). The plan stores a per-cable filter (`PlanStep::noteSourcePortFilters`), so a cable from drumseq *Hat* only delivers hat notes. Single-output sources are unfiltered.
- **Pulse processors.** A `PulseSource` may take Pulse inputs. The plan orders pulse sources upstream-first (`pulseSourceNodeIndices` + `pulseSourcePlans`) and the engine passes the upstream pulses of the same block to `emitPulsesForBeatRange(..., inputPulses, inputPulseCount)`. A pulse cycle is cut: a source only reads slots evaluated before it.
- **Scale.** `quantizesNotesToScale()` defaults to true; drumseq returns false because its pitches are trigger ids.

Oscillator is 8-voice (oldest-age steal, pitch-matched note-off). Sampler still has its own allocator.

## Params and the control fast path

Each adapter declares a `constexpr ParamDescriptor` table (`WasmParams.h`): `{ .id, .name, .minValue, .maxValue, .defaultValue, .kind, .offset, .smoothingMs, .flags }`. The id is the row index (`paramTableIdsAreIndices`) and is stable across builds; `.offset` places the value in the adapter's POD params struct, and `.kind` says how a float is stored there (`Float`, `Int`, `Bool`, ...). A row with no offset is UI-only (serialized, never read by audio). Adapters derive from `AdapterStorage<TParams, TState>` (`AdapterStorage.h`), which generates the block sizes, placement-new and destruction. Port lists are `constexpr PortDescriptor` spans too, so compatibility checks don't allocate.

- **Topology republishes.** Spawn, delete, connect, disconnect, enable and type changes rebuild the snapshot and compile a new plan (`publishAudioGraphSnapshotLocked`).
- **Values don't.** A control change sends `{moduleId, paramId, value, seq}` through the canvas param sink (`ModuleCanvas::setParamSink`): an SPSC queue into the UI module's engine, or the event ring into the worklet engine. `AudioGraphEngine` applies queued changes at block start; params with `smoothingMs > 0` glide with a one-pole, block-rate smoother. A full queue falls back to a republish.
- **Adoption.** Each snapshot carries `generation` and the `paramSeq` it already contains. The engine keeps a short history of applied changes and re-applies any newer than the snapshot after adopting it, so a knob value is never rolled back by a publish that crossed it in flight.
- **Counters.** `__bespoke.getEngineStats()` reports plan compiles, publishes, `prepareRuntimeStates` calls, param changes sent/applied/dropped/stale, and resident resource bytes. A knob drag compiles no plans.

## Stereo bus

Audio buffers are **planar stereo**. Each `AudioProcessPlan` slot owns `2 × blockSize` floats in `AudioGraphEngine::mAudioArena` (L, then R), sized in `prepareForBlock` on the UI thread. Modulation slots stay mono.

- **Channel count per block.** Before `processAudio` the engine fills `context.audioInputs` / `context.inputChannels` and asks `outputChannelCount(params, context)`. The default follows the widest input, so a mono chain (osc → filter → gain → output) stays mono and costs what it did before. The adapter then writes `context.numChannels` buffers in `processAudio(state, params, float* const* channels, context)`; `channels[1]` is null for a mono block.
- **Up/downmix at cable boundaries.** Ports declare `PortChannels::Any | Mono | Stereo`. The plan, not the adapter, picks a `PlanChannelMix` per cable: a stereo cable into a mono port is summed to mid, (L + R) / 2, and a mono cable into a stereo port is L on both sides. A mono slot hands out its L buffer as R (`WasmAudioInput::channels[1] == channels[0]`), so upmixing is free.
- **Who widens.** `panner` (test module: one stereo output, equal-power `pan`), `mixer` (pan), `delay` (`pingpong` > 0 cross-couples the feedback paths), `chorus` (voice A → L, voice B → R, scaled by `width`), `reverb` (Freeverb's L/R tails), and `sampler`/`looper` with stereo material. `gain`, `filter`, `waveshaper`, `adsr`, `delay` process each channel; `compressor` is linked (one detector on max |L|,|R|, one gain) and follows the width of its main input.
- **Output.** The Output sink copies L/R to the device independently. `bespoke_process_audio_block` and offline render still write interleaved stereo; the worklet ring is unchanged.
- **Tests / helpers.** `WasmModuleAdapter::processMono` runs one channel (modulation sources, unit tests). `DspSimd.h` adds `accumulatePanned`, `midDownmix` and `peakAbs` next to the mono kernels; `test_stereo.cpp` checks them against scalar loops.

### Mixer pan

`pan1..4` are in `[0, 1]`, `0.5` = centre (UI shows `L100 … C … R100`). The law is equal-power, normalised to unity at centre: `gL = √2·cos(θ)`, `gR = √2·sin(θ)`, `θ = pan·π/2`. A mono input is therefore +3 dB on the near side when panned hard. A stereo input is balanced instead: each side's gain is capped at 1, so hard-left keeps L at unity and fades R out. Saved v4 patches without `pan*` load centred (see below).

### Output limiter and meter

The Output module has a built-in peak limiter, `limit` on by default (click **LIMIT** to toggle). It is linked across L/R, with instant attack, an 80 ms exponential release, no lookahead and a −0.3 dBFS ceiling (`OutputModuleAdapter::kLimiterCeiling`), so the output never exceeds the ceiling. A non-finite sample is replaced by silence before it reaches the device. With `limit` off, the engine's final ±1 clamp is the only protection (desktop's `OutputChannel` `limit` is that clamp). A patch that stays under the ceiling, such as `?renderTest=1`, passes through unchanged.

The Output VU shows **pre-limit** true peaks per channel: `AudioGraphEngine::takeOutputPeak(ch)` measures the summed Output input before the limiter, on every backend (it no longer reads the SDL backend's post-clip level). The **CLIP** LED lights for about a second whenever a pre-limit peak exceeds 1.0, so it warns even while the limiter is protecting the output.

### Analyzer

`AudioAnalysis::pushStereo` feeds the scope ring with the mid collapse (L + R) / 2 of the device output. Both spectrum paths (`analyzer_fft.wgsl` on WebGPU, `computeSpectrum` on WebGL2) read that ring.

## Scale snapshot

The Scale module stays a singleton UI editor (`WasmAudioRole::None`). `AudioGraphSnapshot::musicScale` copies `{ root, mode }` at publish time. `quantizeMidiPitch` snaps note events when the engine fills `context.notes`. Chromatic mode is identity. A loaded Scala `.scl`/`.kbm` replaces the mode quantizer with a 128-entry note → Hz table (`GraphMusicScale::noteFrequency`); see [live-instrument.md](live-instrument.md#scala-tunings-scl--kbm).

## Missing controls on load

`migratePatchState` fills every control a saved patch lacks with its descriptor default, for current-version patches too. Additive controls (mixer `pan1..4` → 0.5, output `limit` → on, chorus `width` → 1) therefore did not need a schema bump; `kPatchSchemaVersion` stays 4.

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

CC is applied on the UI thread via `ModuleCanvas::applyMidiCc`. Shift-click a control to arm MIDI learn; learned maps are saved in this schema (see [live-instrument.md](live-instrument.md)).

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
| `mixer` | FX | 4× Audio → Audio | `level1..4`, `pan1..4`, `master` | Stereo out. SIMD `accumulatePanned` (mono in) / `accumulateScaled` (stereo in) per port; equal-power pan (see [Stereo bus](#mixer-pan)) |
| `compressor` | FX | Audio In, Audio Sidechain → Audio | `threshold` dB, `ratio`, `attack`/`release` ms, `makeup` dB, `knee` dB | In-adapter feed-forward, soft knee, log-domain ballistics |
| `chorus` | FX | Audio, Mod → Audio | `rate`, `depth`, `delay` ms, `feedback`, `mix`, `width` | 2-voice modulated delay, one 4096-sample ring per channel in runtime state. Voice A → L, voice B → R, blended by `width` (0 = old mono sum; a mono input at width 0 stays mono). Mod CV adds up to ±6 ms |
| `waveshaper` | FX | Audio, Mod (drive) → Audio | `drive`, `mode` (Soft/Tanh/Fold/Hard), `mix`, `level` | Stateless per-sample. Drive CV scales drive by (1 + cv) |
| `reverb` | FX | Audio → Audio | `room`, `damping`, `mix` | `libs/freeverb` `revmodel` (see below). Always stereo out |
| `clockdivider` | Pulse | Pulse → Pulse | `divide` 1–32, `offset` | Passes every Nth pulse |
| `drumseq` | Pulse | Pulse → 4× Note (Kick/Snare/Hat/Tom) | `kick`/`snare`/`hat`/`tom` 16-bit masks, `*pitch`, `steps`, `gate` | Each pulse advances one step. Silent without a Pulse cable |

Sliders use the shared `SliderPanelModule` (one spec table per adapter drives controls, defaults, clamping and UI).

**Reverb / Freeverb.** The `reverb` type keeps its id and controls (saved patches load unchanged) but its kernel is now Jezar's Freeverb from `libs/freeverb` (public domain), the same code the desktop `FreeverbEffect` wraps. The desktop effect class is not compiled. `revmodel` holds pointers into its own buffers, so the adapter detects a runtime-arena move (`boundTo != this`) and rebuilds the model in place. The tail restarts; nothing dangles. It uses 44.1 kHz tunings at every rate.

**juce_dsp spike.** `juce_dsp.h` does compile for wasm (`JUCE_USE_SIMD` resolves to 0, so the SIMD `#error` never fires). It is not header-only, though: `Compressor`/`Chorus`/`DelayLine`/`WaveShaper` bodies and their explicit instantiations live in `juce_dsp.cpp`, which pulls FFT/convolution/oversampling. The compressor and chorus kernels are therefore in-adapter scalar code, and `BESPOKE_WASM_JUCE_DSP` was not added.

Second fixture: `?renderTest=2`. Transport → drumseq; Kick → sine osc (55 Hz), Snare/Hat → ADSRs on one noise source → mixer ch 1–3 → compressor → output. It plays a four-on-the-floor loop with no MIDI (`tests/e2e/drum-bus.spec.ts`).

## Catalog wave 3 (Input / EQ / Note processors)

| Type id | Spawn category | Ports (in → out) | Controls | Behavior |
| --- | --- | --- | --- | --- |
| `input` | Synth | → Audio | `gain` 0–2, `mute` | The mic bus (`context.inputAudio`, mono, see [samples.md](samples.md#looper)) as an Audio source. Only goes where it is cabled, so feedback needs an explicit Input → … → Output cable; there is no separate monitor switch. Shows "Mic off" until the header **Mic** button starts capture, then a level bar |
| `eq` | FX | Audio → Audio | `lowFreq`/`lowGain`, `midFreq`/`midGain`/`midQ`, `hiMidFreq`/`hiMidGain`/`hiMidQ`, `highFreq`/`highGain` (gains ±18 dB), `output` dB | Low shelf, two peaks, high shelf on `Source/BiquadFilter` (the desktop `EQ.cpp` is not compiled). Coefficients are rebuilt only when a control moves; a band within 0.01 dB of flat is skipped, so a flat EQ is a bit-exact bypass. Frequencies slide logarithmically |
| `chorder` | Note | Note → Note | `chord` (Major … Octave, Custom), `inversion`, `tone2..4` (Custom), `level` | One chord per input note. A tone shared by two held chords is not retriggered and stays on until its last key goes |
| `arpeggiator` | Note | Note → Note | `rate` (1/4 … 1/32), `mode` (Up/Down/Up-Down/Random/As played), `octaves` 1–4, `gate` | Walks the held notes on transport beats; silent while the transport is stopped |
| `notefilter` | Note | Note → Note | `low`, `high`, `mode` (Inside/Outside) | Pitch-range gate. A note-off always follows its note-on, so editing the range mid-hold cannot hang a voice |
| `noterouter` | Note | Note → 4× Note | `outputs` 2–4, `mode` (Round robin / Pitch split), `split`, `zone` | Deals notes to outputs; the off follows its on to the same output |

### Note processors

A `NoteProcessor` implements `processNotes(runtime, params, block, out, max, count)`. The engine hands it the merged Note events of its upstream cables **from the same block** and collects what it emits, exactly like pulse processors.

- **Order.** `AudioProcessPlan::noteSourceNodeIndices` holds note sources and processors together, topologically ordered upstream-first (`orderEventSources`, shared with the pulse plan). An `EventSourcePlan` lists the earlier scratch slots it reads (`noteSourceScratchSlots`, with `noteSourcePortFilters` for multi-output sources such as `drumseq` and `noterouter`). A cycle is rejected like any other.
- **Consumers.** Oscillator and sampler read `step.noteSourceScratchSlots` as before, so they work behind any chain without knowing a processor exists. `WasmNoteEvent::outputPort` is how a multi-output source tells its cables apart.
- **No Note cable.** A processor with no incoming Note cable listens to the *performance* (QWERTY and MIDI in). `AudioProcessPlan::performanceNotesProcessed` records that, so MIDI in → chorder → oscillator needs no sequencer. Performance notes are Scale-quantized once, before any processor runs; processors emit exact intervals (`quantizesNotesToScale()` is false).
- **Allocation.** Processor state is a fixed-size runtime block (held-note tables, bitsets), pre-sized with every other module's by `prepareRuntimeStates`. Nothing allocates in `processBlock`; the tests run the whole chain under the allocation trap.
- **Transport stop.** Processor state resets with the transport (`resetEventSourceState`), and the engine ends every note it reported to MIDI out.

### Graph notes to MIDI out

`AudioGraphEngine::setMidiOutCapture(true)` makes `processBlock` copy the notes of every note source and processor into a 512-entry SPSC ring (`drainMidiOut`). Only *reporting* emitters are captured: a source whose Note cables all end in processors is silent here (`EventSourcePlan::reportsToMidiOut`), because the processor's output is what the patch plays. Without that rule a sequencer into a chorder would send both the raw note and the chord. Capture is off unless the host asks, so a patch with no MIDI port pays nothing.

The host side is `GraphNotePump` (`src/app/graphMidiOut.ts`): every 8 ms it follows the port selection (`bespoke_set_graph_note_capture`), drains `bespoke_drain_graph_notes` (one packed int per event), and sends the bytes with a Web MIDI timestamp of `now +` the audio still queued in the worklet ring (or, with the worklet engine, the device latency), so MIDI lands with the sound. With the worklet engine the notes come from the engine's capture through a SharedArrayBuffer ring (`GraphNotePump.setSource`) instead of `bespoke_drain_graph_notes`. Details and the dedup rules: [live-instrument.md](live-instrument.md#midi-out-and-clock).

### Unknown module types

`ModuleCanvas::applyStateSnapshot` (and undo's restore) creates a `PlaceholderModule` for a saved `type` this build lacks, instead of failing or aliasing another module. It has no ports and no DSP; its title is the original type name. Its controls and string extras are kept and written back under the original `type`, and every cable that touched it is kept in `mDanglingConnections` and saved back unchanged (the badge reads "N cables kept, not connected"). The audio graph never sees it. `createModule("vocoder", ...)` from the UI or the C API still fails: placeholders only come from loading. `bespoke_get_placeholder_count()` reports how many a patch loaded.

### Loading a patch

`bespoke_load_state_json` calls `resetAudioGraphEngineForPatch()` (`WasmRuntimeState.cpp`): the live engine is reset in place (`resetClock`: transport clock, queued input notes, every module's runtime state, plus note-offs for anything still sounding on the MIDI output) and its state is re-sized for the new graph. It keeps the block size set at init and its MIDI-out capture setting. Before this, a load swapped in a brand-new `AudioGraphEngine` that was never `prepareForBlock`'d, so `processBlock` returned early and a loaded patch stayed silent (and reported no graph notes) until something else re-prepared the engine.

## Catalog wave 4 (#127: voices, note FX, stereo FX, vocoder, send, modulators)

The issue calls this "catalogue wave 3"; the code and tests call it wave 4 (`test_catalog_wave4.cpp`), after the Input/EQ wave above. Every module has a graph test, a slot in the saved-state fixture, a spawn-menu entry with a description, and hover help ([live-instrument.md](live-instrument.md#module-help)).

| Type id | Spawn category | Ports (in → out) | Behavior |
| --- | --- | --- | --- |
| `fmsynth` | Synth | Note → Audio | Desktop `FMVoice` kernel: carrier, harmonic ratio + tweak, two modulators with their own index envelope. Up to 8 voices (`voices`), oldest stolen |
| `karplusstrong` | Synth | Note → Audio | Plucked string: sine/noise/mix/saw excitation, loop filter with group-delay compensation (pitch within 10 cents), `release` damps after the note-off |
| `seaofgrain` | Synth | Note → Audio (stereo) | Granular voices (16 grains each) over a `SampleStore` buffer; keys set grain pitch (`mode` 1) or position (`mode` 0). Defaults to the bundled tonal pad; drop a file on it to replace |
| `notechance` | Note | Note → Note | Passes each note with probability `chance`; `deterministic` repeats a `length`-step pattern from `seed` |
| `capo` | Note | Note → Note | Transposes by `capo` semitones, or scale degrees with `diatonic`; `retrigger` re-sounds held notes when it moves |
| `pitchbender` | Note | Note → Note | Adds `bend` × `range` semitones to every note, as bend events while notes sound |
| `notesequencer` | Pulse | → Note | 16-step pitch grid with velocity and length lanes; rows walk the Scale (`mode` 0) or semitones |
| `bitcrush` | FX | Audio → Audio | Bit depth (`crush`) and sample-and-hold (`downsamp`) |
| `distortion` | FX | Audio → Audio | Desktop distortion types (clean … grungy), preamp, fuzz, optional input DC removal |
| `stereowidth` | FX | Audio (stereo) → Audio | Mid/side `width` 0–2 plus a Haas delay (`widen`) that widens a mono source |
| `pingpongdelay` | FX | Audio (stereo) → Audio | Tempo-synced (or ms) delay whose echoes alternate left/right; the first echo is left |
| `vocoder` | FX | Carrier + Modulator → Audio | STFT (1024 / hop 256, PFFFT) with log-spaced bands; the carrier takes the modulator's smoothed band envelope |
| `send` | FX | Audio → Out + Send | Splits a signal: `amount` goes to the Send output, `crossfade` dips the main output |
| `envelope` | Mod | Note + Pulse → Mod | ADSR between `low` and `high`. A note-on starts it, the last note-off releases it; a pulse fires a one-shot on its frame |
| `ramper` | Mod | Note + Pulse → Mod | `start` → `target` over `length` beats when triggered, or a transport-locked sawtooth (`mode` 1) |

`eq` now draws its combined response curve (`EqModuleAdapter::responseDb`, `WidgetPainter::drawResponseCurve`).

### Engine additions

- **Pitch bend events.** `WasmNoteEvent::kind == NoteEventKind::PitchBend` re-bends the sounding note at `pitch` (semitones in `pitchBend`); a note-on carries its starting bend. Note processors forward them, and voices fold them into `PolyVoiceSlot::frequency`.
- **Aux outputs.** An adapter may declare up to `kMaxAuxAudioOutputs` (3) audio outputs past its first. The plan gives each a slot (`AudioProcessPlan::auxOutputSlots`) and the adapter writes `context.auxOutputs`. `send` is the first user.
- **Modulation sources with events.** `modulationSourcePlans` (parallel to `modulationSourceNodeIndices`) lists the Note/Pulse cables into each modulation source, so `envelope` and `ramper` see notes and pulses from the same block, upstream-first. Uncabled, they follow the performance notes like note processors do.
- **`gain` Mod input** is additive: `clamp(gain + mod, 0, 1)` per sample (the every-dsp golden was re-recorded once for it).
- The snapshot wire format is version 3 (aux slots and modulation event plans).

### Shared infrastructure

| Piece | File | Used by |
| --- | --- | --- |
| `PolyVoiceAdapter<Derived, Params, Voice>` | `PolyVoiceAdapter.h` | fmsynth, karplusstrong, seaofgrain: allocation and stealing, per-voice amp `dsp::Envelope`, Scale pitch + bend |
| `NoteTransformAdapter<Derived, Params, Extra>` | `NoteTransformAdapter.h` | capo, notechance, pitchbender: one note out per key, note-offs and bends follow the pitch actually sounding |
| Sample source helpers | `SampleSource.h` | sampler, seaofgrain: bind `extras.sampleHash`, rebind in the worklet, report resources, interpolated reads |
| `dsp::RealFft` (PFFFT) | `dsp/Fft.h`, `third_party/pffft` | vocoder and `AudioAnalysis::computeSpectrum` (decision: [module-porting.md](module-porting.md#catalog-wave-4-127)) |
| `dsp::Envelope`, `dsp::SineTable` | `dsp/Envelope.h`, `dsp/SineTable.h` | voices, envelope modulator, FM operators |

## Audio backends (playback)

| Backend | How to select | Notes |
| --- | --- | --- |
| Worklet engine (default) | automatic when `crossOriginIsolated`, or `?audio=worklet` | The graph runs in `BespokeSynthWASM_audio` inside the AudioWorklet; the UI module sends it snapshots and notes. One quantum of buffering |
| Worklet + SAB ring | `?audio=ring`, or automatic if the engine fails to start | UI module renders via `bespoke_process_audio_block` on the main thread; adaptive lookahead |
| SDL2 | `?audio=sdl`, or automatic without isolation | Compatibility path (old Safari, hosts without COOP/COEP) |
| Tone POC | `?audioWorkletPoc=1` | Isolation demo only; not in the header dropdown |

Resolution order: `?audio=` (persisted to `localStorage` `bespokesynth.audio`) → stored preference → auto. A worklet request on a page that is not cross-origin isolated falls back to SDL2; the header shows *Audio: SDL2 (fallback)* with the reason in its tooltip and a status message. `window.__bespoke.getAudioBackendChoice()` returns `{ backend, requested, source, fallbackReason? }`.

Health: `window.__bespoke.getAudioHealth()` (includes `heapBytes`; with the engine also `audioHeapBytes`, `snapshotCount`, `mainThreadBlocks`), `window.__bespoke.getAudioRingStats()` (lookahead), and the `?debug=1` HUD. Offline bounce (Export WAV, `renderOfflineWav`) always renders with the UI module's engine on the main thread. Full design: [../wasm-audio.md](../wasm-audio.md#option-a--audioworklet-engine).

## Production headers (required for the worklet)

The worklet backend needs `SharedArrayBuffer`, which browsers only expose on cross-origin isolated pages. Serve **every** response (HTML, JS, `.wasm`, `.data`, `audio/*.worklet.js`) with:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: same-origin
```

`COEP: require-corp` blocks cross-origin subresources that do not send CORP/CORS headers. Keep assets same-origin, or have the other origin send `Cross-Origin-Resource-Policy: cross-origin`. Without these headers the app still works on SDL2. Server snippets: [../DEPLOYMENT_GUIDE.md](../DEPLOYMENT_GUIDE.md#cross-origin-isolation-headers-audioworklet). Pthreads stay off; these headers are only for SAB.

## Adding note-driven modules

Prefer a new `WasmModuleAdapter` with `WasmAudioRole::NoteSource`, `NoteProcessor` or `PulseSource` (or consume notes/pulses in `processAudio` via `WasmAudioProcessContext`). Do not special-case types inside `ModuleCanvas`. See [module-porting.md](module-porting.md).
