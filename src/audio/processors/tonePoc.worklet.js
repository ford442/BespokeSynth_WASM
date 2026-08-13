/**
 * AudioWorkletProcessor: continuous tone for isolation POC.
 * Posts { type: 'frames', frames } periodically so the main thread can
 * verify audio kept advancing during a deliberate stall.
 */
class TonePocProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this._phase = 0;
    this._frames = 0;
    this._freq = 440;
    this._gain = 0.15;
    this._running = true;
    this.port.onmessage = (ev) => {
      const data = ev.data || {};
      if (data.type === 'stop') this._running = false;
      if (data.type === 'start') this._running = true;
      if (typeof data.freq === 'number') this._freq = data.freq;
    };
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
    const sr = sampleRate;
    const twoPi = Math.PI * 2;
    for (let i = 0; i < frames; ++i) {
      const s = Math.sin(this._phase) * this._gain;
      left[i] = s;
      right[i] = s;
      this._phase += (twoPi * this._freq) / sr;
      if (this._phase > twoPi) this._phase -= twoPi;
    }
    this._frames += frames;
    if (this._frames - (this._lastPosted || 0) >= 128) {
      this._lastPosted = this._frames;
      this.port.postMessage({ type: 'frames', frames: this._frames });
    }
    return true;
  }
}

registerProcessor('bespoke-tone-poc', TonePocProcessor);
