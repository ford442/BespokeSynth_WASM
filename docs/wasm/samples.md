# Samples, looper, and export

The WASM host can import audio, play it from a Sampler, record a transport-quantized loop, and bounce the compiled graph faster than realtime.

## Import

- Drag a WAV/FLAC/MP3 onto the canvas, or use **Sample**.
- Decode happens in C++ (`dr_wav` / `dr_flac` / `dr_mp3`) on the UI thread, then Catmull-Rom resample to the device rate.
- PCM lives in `SampleStore` as an immutable `SampleBuffer`. The audio thread only sees a `const SampleBuffer*` copied into the published graph snapshot.
- Original file bytes are stored in OPFS keyed by SHA-256. Patch JSON stores `extras.sampleHash`, not PCM.

## Sampler

Note-triggered, 8-voice, allocation-free callback. Modes: one-shot, loop, gate. Waveform is a min/max peak cache computed at load.

## Looper

Record / overdub / play, length quantized to bars from transport BPM. The record arena is preallocated on the UI thread (`LooperArenaPool`). Live input arrives through `bespoke_push_input_audio` (header **Mic** button).

## Offline render

`bespoke_render_offline(seconds, sampleRate)` runs the same `processBlock` plan with no device and writes a WAV (float32 default, 16- or 24-bit optional). **Export WAV** downloads it. Two renders of the same graph are bit-identical (see `test_sample_io_and_offline`).

## Share

**Share** writes `#p=` plus deflated base64url patch JSON. Opening that URL in a clean profile reloads the modules. Patches that reference samples also need those hashes in OPFS, or a `.bspk` bundle (`src/samples/bspkBundle.ts`).
