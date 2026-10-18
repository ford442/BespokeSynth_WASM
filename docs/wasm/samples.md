# Samples, looper, and export

The WASM host can import audio, play it from a Sampler, record a transport-quantized loop, and bounce the compiled graph faster than realtime.

## Import

- Drag a WAV/FLAC/MP3 onto the canvas, or use **Sample**.
- Decode happens in C++ (`dr_wav` / `dr_flac` / `dr_mp3`) on the UI thread, then each channel is Catmull-Rom resampled to the device rate.
- PCM lives in `SampleStore` as an immutable `SampleBuffer`: planar float, `channelCount()` 1 or 2 (`channelData(ch)`). Mono and stereo files keep their channels; wider files are downmixed to mono. The audio thread only sees a `const SampleBuffer*` copied into the published graph snapshot.
- Original file bytes are stored in OPFS keyed by SHA-256. Patch JSON stores `extras.sampleHash`, not PCM. The hash covers the original file bytes, so the content-addressed format is unchanged by the stereo PCM layout (previously stored samples decode as stereo on next load).

## Sampler

Note-triggered, 8-voice, allocation-free callback. Modes: one-shot, loop, gate. A stereo file plays stereo (the module's output is 2 channels); a mono file stays a mono source. Waveform is a min/max peak cache over both channels, computed at load.

## Looper

Record / overdub / play, length quantized to bars from transport BPM. The record arena is preallocated on the UI thread (`LooperArenaPool`). Live input arrives through `bespoke_push_input_audio` (header **Mic** button): `src/audio/inputCapture.ts` runs `src/audio/processors/micCapture.worklet.js` as an AudioWorklet, which batches 512-frame mono chunks and posts them to the main thread, which pushes them into `InputAudioBus`. No SharedArrayBuffer is needed, so it works without cross-origin isolation.

The arena is planar stereo (2 × 16 s at 48 kHz, ~6 MB per looper). A loop records at the width of its audio input (the mic bus is mono); overdubbing a stereo input onto a mono loop widens it by copying L to R first.

## Offline render

`bespoke_render_offline(seconds, sampleRate)` runs the same `processBlock` plan with no device and writes a WAV (float32 default, 16- or 24-bit optional). **Export WAV** downloads it. Two renders of the same graph are bit-identical (see `test_sample_io_and_offline`).

## Share

**Share** writes `#p=` plus deflated base64url patch JSON. Opening that URL in a clean profile reloads the modules. Patches that reference samples also need those hashes in OPFS, or a `.bspk` bundle (`src/samples/bspkBundle.ts`).
