/**
 * AudioWorkletProcessor: consume SharedArrayBuffer stereo ring.
 * Header layout must match src/audio/ringBuffer.ts
 */
const HEADER_INTS = 16;
const HEADER_BYTES = HEADER_INTS * 4;
const CHANNELS = 2;
const IDX_WRITE = 0;
const IDX_READ = 1;
const IDX_UNDERRUN = 2;
const IDX_CAPACITY = 3;
const IDX_RUNNING = 7;

class RingPlayerProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const sab = options.processorOptions && options.processorOptions.sab;
    this._ready = false;
    if (sab) {
      this._header = new Int32Array(sab, 0, HEADER_INTS);
      this._samples = new Float32Array(sab, HEADER_BYTES);
      this._ready = true;
    }
    this._framesOut = 0;
    this.port.onmessage = (ev) => {
      const data = ev.data || {};
      if (data.type === 'sab' && data.sab) {
        this._header = new Int32Array(data.sab, 0, HEADER_INTS);
        this._samples = new Float32Array(data.sab, HEADER_BYTES);
        this._ready = true;
      }
    };
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!output || !output[0]) return true;
    const left = output[0];
    const right = output[1] || output[0];
    const frames = left.length;

    if (!this._ready || Atomics.load(this._header, IDX_RUNNING) === 0) {
      left.fill(0);
      if (right !== left) right.fill(0);
      return true;
    }

    const capacity = Atomics.load(this._header, IDX_CAPACITY);
    let read = Atomics.load(this._header, IDX_READ);
    const write = Atomics.load(this._header, IDX_WRITE);
    let avail = write - read;
    if (avail < 0) avail += capacity;

    for (let i = 0; i < frames; ++i) {
      if (avail <= 0) {
        left[i] = 0;
        right[i] = 0;
        Atomics.add(this._header, IDX_UNDERRUN, 1);
      } else {
        const src = read * CHANNELS;
        left[i] = this._samples[src];
        right[i] = this._samples[src + 1];
        read += 1;
        if (read >= capacity) read = 0;
        avail -= 1;
      }
    }
    Atomics.store(this._header, IDX_READ, read);
    this._framesOut += frames;
    if ((this._framesOut & 2047) === 0) {
      this.port.postMessage({
        type: 'stats',
        frames: this._framesOut,
        underruns: Atomics.load(this._header, IDX_UNDERRUN),
        depth: avail,
      });
    }
    return true;
  }
}

registerProcessor('bespoke-ring-player', RingPlayerProcessor);
