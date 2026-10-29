/**
 * AudioWorkletProcessor: batch mono microphone input into fixed-size chunks.
 *
 * The WASM heap is not shared with the worklet thread, so each full chunk is
 * posted (transferred, not copied) to the main thread, which pushes it into
 * the C++ InputAudioBus via _bespoke_push_input_audio. No SharedArrayBuffer is
 * needed, so this works without cross-origin isolation.
 *
 * Messages out: { type: 'chunk', samples: Float32Array } of `chunkFrames` frames.
 * Messages in:  { type: 'stop' } ends the processor.
 */
const DEFAULT_CHUNK_FRAMES = 512;

class MicCaptureProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const requested = options && options.processorOptions && options.processorOptions.chunkFrames;
    this._chunkFrames = requested > 0 ? requested | 0 : DEFAULT_CHUNK_FRAMES;
    this._chunk = new Float32Array(this._chunkFrames);
    this._filled = 0;
    this._stopped = false;
    this.port.onmessage = (ev) => {
      if (ev.data && ev.data.type === 'stop') this._stopped = true;
    };
  }

  process(inputs) {
    if (this._stopped) return false;
    // No channel while the mic is not (yet) delivering: nothing to batch, keep running.
    const input = inputs[0] && inputs[0][0];
    if (!input) return true;

    let offset = 0;
    while (offset < input.length) {
      const n = Math.min(input.length - offset, this._chunkFrames - this._filled);
      this._chunk.set(input.subarray(offset, offset + n), this._filled);
      this._filled += n;
      offset += n;
      if (this._filled === this._chunkFrames) {
        this.port.postMessage({ type: 'chunk', samples: this._chunk }, [this._chunk.buffer]);
        this._chunk = new Float32Array(this._chunkFrames);
        this._filled = 0;
      }
    }
    return true;
  }
}

registerProcessor('bespoke-mic-capture', MicCaptureProcessor);
