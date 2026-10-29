/**
 * AudioWorkletProcessor: batch mono microphone input into fixed-size chunks.
 *
 * Main-thread engine: the WASM heap is not shared with the worklet thread, so
 * each full chunk is posted (transferred, not copied) to the main thread, which
 * pushes it into the C++ InputAudioBus via _bespoke_push_input_audio. No
 * SharedArrayBuffer is needed, so this works without cross-origin isolation.
 *
 * Worklet engine (processorOptions.ring): samples go straight into the engine's
 * SPSC input ring (src/audio/engineShared.ts layout: Int32 [write, read,
 * capacity, dropped], then floats), so the microphone never touches the main
 * thread. A full ring drops samples and counts them.
 *
 * Messages out: { type: 'chunk', samples: Float32Array } of `chunkFrames` frames.
 * Messages in:  { type: 'stop' } ends the processor.
 */
const DEFAULT_CHUNK_FRAMES = 512;
const RING_WRITE = 0;
const RING_READ = 1;
const RING_CAPACITY = 2;
const RING_DROPPED = 3;
const RING_HEADER_BYTES = 16;

class MicCaptureProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = (options && options.processorOptions) || {};
    const requested = o.chunkFrames;
    this._chunkFrames = requested > 0 ? requested | 0 : DEFAULT_CHUNK_FRAMES;
    this._ringHeader = o.ring ? new Int32Array(o.ring, 0, 4) : null;
    this._ringFloats = o.ring ? new Float32Array(o.ring, RING_HEADER_BYTES) : null;
    this._chunk = this._ringHeader ? null : new Float32Array(this._chunkFrames);
    this._filled = 0;
    this._stopped = false;
    this.port.onmessage = (ev) => {
      if (ev.data && ev.data.type === 'stop') this._stopped = true;
    };
  }

  _writeRing(input) {
    const header = this._ringHeader;
    const capacity = Atomics.load(header, RING_CAPACITY);
    const read = Atomics.load(header, RING_READ);
    let write = Atomics.load(header, RING_WRITE);
    for (let i = 0; i < input.length; ++i) {
      const next = write + 1 >= capacity ? 0 : write + 1;
      if (next === read) {
        Atomics.add(header, RING_DROPPED, input.length - i);
        break;
      }
      this._ringFloats[write] = input[i];
      write = next;
    }
    Atomics.store(header, RING_WRITE, write);
  }

  process(inputs) {
    if (this._stopped) return false;
    // No channel while the mic is not (yet) delivering: nothing to batch, keep running.
    const input = inputs[0] && inputs[0][0];
    if (!input) return true;
    if (this._ringHeader) {
      this._writeRing(input);
      return true;
    }

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
