/**
 * AudioWorkletProcessor: plays interleaved stereo chunks the main thread posts over the node's
 * MessagePort. The player for pages that cannot use SharedArrayBuffer (no COOP/COEP), so output
 * still runs on the audio thread instead of Emscripten's deprecated ScriptProcessorNode.
 *
 * Main -> worklet
 *   { type: 'run', running, epoch }  start (or stop) playing; drops everything queued
 *   { type: 'chunk', epoch, samples } interleaved L/R Float32Array; ignored for an old epoch
 * Worklet -> main (a few times per 100 ms, only while running)
 *   { type: 'stats', epoch, consumed, underruns }
 *     consumed  frames played since 'run'; the producer's queue depth is frames sent - consumed
 *     underruns frames of silence since the first chunk arrived, cumulative over the node's life
 *
 * The SharedArrayBuffer player (ringPlayer.worklet.js) keeps these counters in shared memory and
 * posts nothing from the render thread; this one pays one small message per STATS_QUANTA.
 */
const CHANNELS = 2;
// 4 quanta = 512 frames (~11 ms at 44.1 kHz): the producer sees its queue depth with that much delay.
const STATS_QUANTA = 4;

class PortPlayerProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this._queue = [];
    this._head = 0;
    this._pos = 0;
    this._running = false;
    this._primed = false;
    this._epoch = 0;
    this._consumed = 0;
    this._underruns = 0;
    this._quanta = 0;
    this.port.onmessage = (event) => {
      const data = event.data || {};
      if (data.type === 'run') {
        this._epoch = data.epoch | 0;
        this._running = !!data.running;
        this._primed = false;
        this._consumed = 0;
        this._quanta = 0;
        this._clear();
      } else if (data.type === 'chunk') {
        if ((data.epoch | 0) !== this._epoch || !this._running || !data.samples) return;
        if (data.samples.length >= CHANNELS) this._queue.push(data.samples);
        this._primed = true;
      }
    };
  }

  _clear() {
    this._queue.length = 0;
    this._head = 0;
    this._pos = 0;
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!output || !output[0]) return true;
    const left = output[0];
    const right = output[1] || output[0];
    const frames = left.length;

    if (!this._running) {
      left.fill(0);
      if (right !== left) right.fill(0);
      return true;
    }

    let written = 0;
    while (written < frames && this._head < this._queue.length) {
      const chunk = this._queue[this._head];
      const chunkFrames = chunk.length >> 1;
      const start = this._pos;
      const take = Math.min(frames - written, chunkFrames - start);
      for (let i = 0; i < take; ++i) {
        const src = (start + i) * CHANNELS;
        left[written + i] = chunk[src];
        right[written + i] = chunk[src + 1];
      }
      written += take;
      this._pos = start + take;
      if (this._pos >= chunkFrames) {
        this._head += 1;
        this._pos = 0;
        if (this._head >= this._queue.length) {
          this._queue.length = 0;
          this._head = 0;
        }
      }
    }
    this._consumed += written;
    if (written < frames) {
      for (let i = written; i < frames; ++i) {
        left[i] = 0;
        if (right !== left) right[i] = 0;
      }
      // Silence before the first chunk of a run is the producer still starting, not an underrun.
      if (this._primed) this._underruns += frames - written;
    }

    this._quanta += 1;
    if (this._quanta >= STATS_QUANTA) {
      this._quanta = 0;
      this.port.postMessage({
        type: 'stats',
        epoch: this._epoch,
        consumed: this._consumed,
        underruns: this._underruns,
      });
    }
    return true;
  }
}

registerProcessor('bespoke-port-player', PortPlayerProcessor);
