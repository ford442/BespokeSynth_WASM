# ADR: WASM Module Porting Strategy

**Status:** Accepted

**Decision:** Use incremental ports backed by a WASM module adapter layer. The adapter owns the browser UI and graph ports while reusing desktop DSP kernels only when their dependency closure is WASM-safe. Do not compile desktop `IDrawableModule` subclasses into the browser target as the default path.

## Context

The desktop factory in `Source/ModuleFactory.cpp` registers a broad desktop catalog. `Source/CMakeLists.txt` enumerates 369 C++ implementation files, including the module factory, UI controls, save/load, hardware integrations, and plug-in hosting. The WASM canvas currently has a small native module hierarchy and cannot safely claim desktop module parity.

The important distinction is oscillator support:

| Surface | Current state | Dependency result |
| --- | --- | --- |
| `Source/Oscillator.cpp` | Already in `wasm/CMakeLists.txt` `CORE_SOURCES` | Compiles as reusable DSP kernel |
| `Source/SingleOscillator.cpp` | Desktop visual/instrument module | Blocked by `IDrawableModule`, controls, note/polyphony plumbing, OpenFrameworks, NanoVG, and desktop save UI |
| WASM `OscillatorModule` | Native canvas module | Uses a limited local oscillator implementation and graph contract |

Directly adding `ModuleFactory.cpp` would pull every registered module header into the compilation closure. That is not a minimum compile set and would turn feature delivery into a platform-port of the desktop UI framework.

## Decision

Create `WasmModuleAdapter` as the future boundary between `ModuleCanvas::Module` and desktop-derived DSP components.

```text
ModuleCanvas port/control state
        |
WasmModuleAdapter (WASM UI, serialization, graph contract)
        |
WASM-safe DSP kernel from Source/ or a compatible implementation
        |
AudioGraphEngine::processBlock()
```

Adapters must expose typed input/output ports, controls, block processing, and a serializable state map. They must not depend on `IDrawableModule`, NanoVG, OpenFrameworks drawing, VST, MIDI hardware, or desktop-global UI state.

Initial priority order:

1. Oscillator: replace the local waveform implementation with the existing `Oscillator` DSP kernel where behavior matches.
2. Filter: retain/reuse `BiquadFilter` with adapter-owned cutoff and resonance controls.
3. Gain and output: keep lightweight WASM adapters; no desktop UI reuse is required.
4. Sequencer: define note/pulse block events before choosing a desktop DSP subset.
5. Sampler: defer until browser asset loading, decode, memory limits, and persistence semantics are defined.

## Alternatives Considered

### A. Incremental adapter ports

Accepted. It makes parity explicit per module, bounds each dependency closure, and preserves the WASM renderer and graph as the browser authority.

### B. Shared header-only audio abstraction

Deferred. Extracting `IAudioSource`/`IAudioReceiver` may become useful after three adapters demonstrate common duplication. Doing it first risks moving desktop global assumptions into a nominally shared API.

### C. Permanently decoupled hierarchy

Rejected as the long-term direction. It permits fast demos but makes behavioral parity, patch compatibility, and maintenance progressively more expensive.

## Dependency Audit

| Category | Examples | Primary blockers | Port rule |
| --- | --- | --- | --- |
| DSP effects/sources | `Oscillator`, `BiquadFilter`, `ADSR`, `LFO` | `SynthGlobals`, OpenFrameworks math helpers | Reuse after a narrow WASM compatibility audit |
| Drawable audio modules | `SingleOscillator`, effect modules | `IDrawableModule`, sliders, dropdowns, NanoVG, JSON UI/save state | Rebuild UI as an adapter; do not compile desktop drawable class |
| Note/pulse sequencers | `StepSequencer`, `NoteStepSequencer` | note buffers, timing globals, UI grids, persistence | Define WASM event graph and port one behavior at a time |
| Sample/file modules | `Sampler`, `SamplerGrid` | filesystem, decode, waveform UI, memory use | Browser asset/persistence design required first |
| Integrations | VST, MIDI controllers, hardware modules | native APIs, device access, plugin hosting | Out of scope for the WASM parity roadmap |

## Prototype Result

The DSP oscillator prototype already succeeds: `Source/Oscillator.cpp` is compiled by the WASM target. The desktop `SingleOscillator` prototype is intentionally not added because its header directly includes `IAudioSource`, `PolyphonyMgr`, `INoteReceiver`, `IDrawableModule`, `Slider`, `DropdownList`, `ADSRDisplay`, `Checkbox`, `RadioButton`, and `IControlVisualizer`. This validates the adapter decision without introducing a misleading partial desktop build.

## Size Budget and Measurement

The current optimized artifact is approximately 1.4 MB (`wasm/dist/BespokeSynthWASM.wasm`). Before every group of ten adapters, record a baseline and post-build size:

```bash
stat -c '%s' wasm/dist/BespokeSynthWASM.wasm
npm run build:wasm
stat -c '%s' wasm/dist/BespokeSynthWASM.wasm
```

Report absolute bytes, delta bytes, and the retained module list in the PR. The initial planning budget is **+250 KB compressed-equivalent per ten DSP-only adapters**; sampler, FFT, and asset-heavy modules require separate budgets. No extrapolated estimate is recorded before the first ten-adapter measurement.

## Consequences

- `ModuleCanvas` remains the source of truth for WASM interaction and serialization.
- Desktop module names may be reused only when the adapter documents supported controls and port semantics.
- Unknown desktop module types load as explicit placeholders, not as a different DSP behavior (implemented in wave 3: [wasm/audio.md](wasm/audio.md#unknown-module-types)). Never alias an unknown name onto `oscillator` or any other adapter.
- A new module must include a graph/audio test and a saved-state compatibility fixture before it is marked ported.

## Implementation status (2026-08)

The adapter boundary is implemented in code:

- `WasmModuleAdapter` + `WasmModuleAdapterRegistry` register module metadata, UI factories, typed parameter blocks, ports, and DSP.
- **Filter** is the first adapter with end-to-end `processAudio` using `Source/BiquadFilter` (`FilterModuleAdapter`).
- **ADSR**, **Delay**, **Noise**, **Oscillator**, **Gain**, **LFO**, **Output**, **Transport**, and **Scale** are first-class adapters (no `SimpleWasmModuleAdapter` lambda pile).
- **Step Sequencer** (`stepsequencer`) is the first `NoteSource` adapter: transport-synced 16-step grid, Note out → instrument Pitch, pattern/pitch/gate/steps in patch JSON. Desktop sequencers are reference-only (not compiled into WASM).
- **Reverb** is the first module added under the self-registration rule: one header, one source, one CMake line (`BESPOKE_REGISTER_MODULE` + `BESPOKE_WASM_ADAPTER_SOURCES`).
- `AudioGraphEngine` walks the compiled process plan and calls `adapter->processAudio(...)`. Module parameters live in adapter-defined PODs in a snapshot param arena; runtime state is an adapter-sized arena, not a struct-of-everything.

### How to add a module

1. Add `wasm/include/BespokeWasm/adapters/FooModuleAdapter.h` with a POD `FooParams`, optional UI class, and `FooModuleAdapter`.
2. Add `wasm/src/adapters/FooModuleAdapter.cpp` implementing:
   - `typeId`, `displayName`, `category`, `audioRole`
   - `controlDescriptors`, `inputPorts`, `outputPorts`, `createUiModule`
   - `paramsSize` / `fillParams`
   - `runtimeStateSize` / `initRuntimeState` / `destroyRuntimeState` / `processAudio` when the module owns DSP
   - `BESPOKE_REGISTER_MODULE(FooModuleAdapter);`
3. Add one line to `BESPOKE_WASM_ADAPTER_SOURCES` in `wasm/CMakeLists.txt`.
4. Add graph and/or adapter unit tests in `wasm/tests/` (pick the matching `test_*.cpp`, declare in `test_common.h`, call from `test_main.cpp`).
5. Saved patches stay control-name JSON; bump `kPatchSchemaVersion` only when the on-disk map changes, and extend `migratePatchState()`.

For a flat list of ranged controls, build the UI from a `SliderSpec` table (`SliderPanelModule`) and derive `controlDescriptors()` / `fillParams` from the same table (`controlDescriptorsFromSpecs`, `sliderSpecValue`). Multi-port audio, multi-output notes and pulse processors are covered in [wasm/audio.md](wasm/audio.md#per-port-routing); note processors (`WasmAudioRole::NoteProcessor`, `processNotes`) in [wasm/audio.md](wasm/audio.md#note-processors).

### Catalog wave 2 size record (2026-09)

Measured Release (`-O3`, LTO, SIMD) `wasm/dist/BespokeSynthWASM.wasm`, gzip -9 as the compressed-equivalent:

| Build | Raw bytes | gzip bytes |
| --- | --- | --- |
| Before wave 2 | 839,874 | 305,673 |
| After wave 2 | 857,158 | 315,988 |
| **Delta** | **+17,284** | **+10,315** |

Leave-one-out line items (raw / gzip). LTO makes these non-additive:

| Item | Raw | gzip |
| --- | --- | --- |
| `drumseq` (adapter + grid UI) | +6,245 | +1,940 |
| `compressor` | +2,700 | +898 |
| Freeverb in place of the Schroeder reverb | +2,764 | +810 |
| `waveshaper` | +2,052 | +896 |
| `mixer` | +1,799 | +703 |
| `chorus` | +1,575 | +721 |
| `clockdivider` | +997 | +400 |

Retained modules: oscillator, noise, sampler, filter, gain, delay, adsr, lfo, reverb (freeverb), looper, stepsequencer, pulsetonote, transport, scale, output, **mixer, compressor, chorus, waveshaper, clockdivider, drumseq**. That is well inside the +250 KB compressed per ten DSP adapters budget.

### Catalog wave 3 size record (2026-10)

Measured Release (`-O3`, SIMD, **LTO off**) `BespokeSynthWASM.wasm` and `.data`, gzip -9 as the compressed-equivalent. The baseline is an LTO-off build of the tree just before the wave (the wave-2 table above is an LTO build, so the two tables do not chain):

| Artifact | Before: raw | Before: gzip | After: raw | After: gzip | Delta raw | Delta gzip |
| --- | --- | --- | --- | --- | --- | --- |
| `BespokeSynthWASM.wasm` | 1,344,460 | 478,006 | 1,385,212 | 493,296 | **+40,752** | **+15,290** |
| `BespokeSynthWASM.data` | 93,428 | 36,775 | 199,102 | 132,068 | **+105,674** | **+95,293** |

- The `.wasm` delta covers six DSP/note adapters (`input`, `eq`, `chorder`, `arpeggiator`, `notefilter`, `noterouter`), the placeholder module, the note-processor plan and engine loop, the MIDI-out ring and bridge calls, the bundled-kit loader, and the spawn-menu chips. It is far inside the +250 KB compressed per ten DSP adapters budget.
- The `.data` delta is the drum kit, billed separately from the DSP budget: four mono 16-bit 44.1 kHz WAVs under `wasm/resource-pack/kits/basic/` (kick 33,560, snare 21,212, hat 9,746, tom 37,088 bytes; 101,606 total, about 94 KB gzipped) plus the ~4 KB `wasm-drumkit.bsk` patch. `scripts/gen_drum_kit.py` regenerates the WAVs byte-for-byte.
- The kit is addressed by content hash (`BundledKit.cpp`), so patches reference `extras.sampleHash` like any user sample and the sampler needs no special case. It is decoded lazily at the device rate in use when a sampler first asks for it.

Retained modules: oscillator, noise, sampler, filter, gain, delay, adsr, lfo, reverb (freeverb), looper, stepsequencer, pulsetonote, transport, scale, output, mixer, compressor, chorus, waveshaper, clockdivider, drumseq, script, **input, eq, chorder, arpeggiator, notefilter, noterouter**.

Deferred from the wave-3 table: a graph `midioutput` module. Graph notes reach the selected Web MIDI port through the host pump instead ([wasm/live-instrument.md](wasm/live-instrument.md#midi-out-and-clock)), which covers the acceptance cases without a second place to configure the port.

## Issue Comment Summary

Use the following decision summary in the tracking issue: **Accepted A, incremental adapter ports. `Oscillator.cpp` is already WASM-safe DSP, but `SingleOscillator` is a desktop UI module with a large `IDrawableModule` dependency closure. We will port behavior through WASM adapters, measure binary growth per ten modules, and defer shared-interface extraction until adapter duplication demonstrates a stable seam.**
