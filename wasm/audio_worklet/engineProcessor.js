/**
 * AudioWorkletProcessor for the DSP-only engine module (Option A, docs/wasm-audio.md).
 *
 * CMake appends this file to BespokeSynthWASM_audio.js (--extern-post-js), so one
 * audioWorklet.addModule() defines both createBespokeAudioEngine and the processor.
 * The worklet has no fetch: the host compiles the .wasm on the main thread and
 * passes the WebAssembly.Module in processorOptions.
 *
 * Shared memory (layouts mirror src/audio/engineShared.ts):
 *   events   main -> worklet   SPSC ring of 4-word note/script records
 *   input    mic  -> worklet   SPSC ring of mono float samples
 *   midiOut  worklet -> main   SPSC ring of packed graph notes for the MIDI output
 *   analysis worklet -> main   overwrite ring of mono (mid) output samples (scope/FFT)
 *   status   both              int32 flags/peaks + float64 telemetry
 * Messages in: { type: 'snapshot', bytes, reset } and { type: 'sample', id, channels,
 * frames, rate, pcm }. Messages out: { type: 'ready', abi } or { type: 'error', message }.
 * process() never allocates: every view over the WASM heap is cached and only rebuilt
 * after the heap grows (which only happens while applying a message).
 */

// AudioWorkletGlobalScope has no performance.now() in Chrome (WebAudio #2413); the
// engine's steady_clock reads it. Date.now() is coarse but unbiased on average.
if (typeof globalThis.performance === 'undefined') {
  const origin = Date.now();
  globalThis.performance = { now: () => Date.now() - origin };
}

const RING_WRITE = 0;
const RING_READ = 1;
const RING_CAPACITY = 2;
const RING_DROPPED = 3;
const RING_HEADER_BYTES = 16;

const STATUS_INTS = 16;
const STATUS_DOUBLES_OFFSET = STATUS_INTS * 4;
const STATUS_DOUBLES = 16;
const ST_READY = 0;
const ST_PEAK_L = 1;
const ST_PEAK_R = 2;
const ST_PEAK_INPUT = 3;
const ST_MIDI_CAPTURE = 4;
const ST_INPUT_ACTIVE = 5;
const ST_SEQ = 6;

// AudioEngineStatusSlot (wasm/include/BespokeWasm/AudioWorkletEngine.h).
const STATUS_SLOT_COUNT = 14;
const SLOT_PEAK_L = 10;
const SLOT_PEAK_R = 11;
const SLOT_PEAK_INPUT = 12;

const EVENT_WORDS = 4;
const MAX_EVENTS_PER_QUANTUM = 256;
const MAX_INPUT_PER_QUANTUM = 4096;
const MIDI_BATCH = 64;
const MAX_BLOCK = 2048;

function ringViews(sab) {
  if (!sab) return null;
  return {
    header: new Int32Array(sab, 0, 4),
    words: new Int32Array(sab, RING_HEADER_BYTES),
    floats: new Float32Array(sab, RING_HEADER_BYTES),
  };
}

class BespokeAudioEngineProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = (options && options.processorOptions) || {};
    this._m = null;
    this._failed = false;
    this._pending = [];
    this._events = ringViews(o.events);
    this._input = ringViews(o.input);
    this._midiOut = ringViews(o.midiOut);
    this._statusI = new Int32Array(o.status, 0, STATUS_INTS);
    this._statusD = new Float64Array(o.status, STATUS_DOUBLES_OFFSET, STATUS_DOUBLES);
    this._analysisHeader = new Int32Array(o.analysis, 0, 4);
    this._analysis = new Float32Array(o.analysis, RING_HEADER_BYTES);
    this._bitsF = new Float32Array(1);
    this._bitsI = new Int32Array(this._bitsF.buffer);
    this._midiCapture = 0;
    this._inputActive = 0;
    this._heap = null;
    this.port.onmessage = (event) => this._onMessage(event.data || {});

    if (typeof createBespokeAudioEngine !== 'function' || !o.wasmModule) {
      this._fail('engine module missing from processorOptions');
      return;
    }
    createBespokeAudioEngine({
      instantiateWasm: (imports, receiveInstance) => {
        WebAssembly.instantiate(o.wasmModule, imports).then(
          (instance) => receiveInstance(instance, o.wasmModule),
          (error) => this._fail(error),
        );
        return {};
      },
      print: (text) => console.log(text),
      printErr: (text) => console.error(text),
    }).then(
      (m) => this._start(m, o),
      (error) => this._fail(error),
    );
  }

  _fail(error) {
    this._failed = true;
    Atomics.store(this._statusI, ST_READY, -1);
    this.port.postMessage({ type: 'error', message: String((error && error.message) || error) });
  }

  _start(m, o) {
    const abi = m._bespoke_audio_wire_abi();
    if (typeof o.wireAbi === 'number' && o.wireAbi !== abi) {
      this._fail(`snapshot ABI mismatch: UI module ${o.wireAbi}, audio module ${abi}`);
      return;
    }
    m._bespoke_audio_init(sampleRate, MAX_BLOCK);
    this._left = m._malloc(MAX_BLOCK * 4);
    this._right = m._malloc(MAX_BLOCK * 4);
    this._eventPtr = m._malloc(MAX_EVENTS_PER_QUANTUM * EVENT_WORDS * 4);
    this._inputPtr = m._malloc(MAX_INPUT_PER_QUANTUM * 4);
    this._midiPtr = m._malloc(MIDI_BATCH * 4);
    this._statusPtr = m._malloc(STATUS_SLOT_COUNT * 8);
    this._stagingPtr = 0;
    this._stagingBytes = 0;
    this._m = m;
    this._refreshViews(128);
    const pending = this._pending;
    this._pending = [];
    for (const data of pending) this._onMessage(data);
    Atomics.store(this._statusI, ST_READY, 1);
    this.port.postMessage({ type: 'ready', abi });
  }

  /** (Re)builds the cached heap views; the heap buffer only changes after growth. */
  _refreshViews(frames) {
    const m = this._m;
    const buffer = m.HEAPU8.buffer;
    this._heap = buffer;
    this._frames = frames;
    this._leftView = new Float32Array(buffer, this._left, frames);
    this._rightView = new Float32Array(buffer, this._right, frames);
    this._eventView = new Int32Array(buffer, this._eventPtr, MAX_EVENTS_PER_QUANTUM * EVENT_WORDS);
    this._inputView = new Float32Array(buffer, this._inputPtr, MAX_INPUT_PER_QUANTUM);
    this._midiView = new Int32Array(buffer, this._midiPtr, MIDI_BATCH);
    this._statusView = new Float64Array(buffer, this._statusPtr, STATUS_SLOT_COUNT);
  }

  _onMessage(data) {
    if (!this._m) {
      if (!this._failed) this._pending.push(data);
      return;
    }
    const m = this._m;
    if (data.type === 'snapshot' && data.bytes) {
      const bytes = new Uint8Array(data.bytes);
      if (bytes.length > this._stagingBytes) {
        if (this._stagingPtr) m._free(this._stagingPtr);
        this._stagingBytes = Math.max(bytes.length, this._stagingBytes * 2, 4096);
        this._stagingPtr = m._malloc(this._stagingBytes);
      }
      m.HEAPU8.set(bytes, this._stagingPtr);
      if (m._bespoke_audio_apply_snapshot(this._stagingPtr, bytes.length, data.reset ? 1 : 0) !== 0) {
        this.port.postMessage({ type: 'error', message: m.UTF8ToString(m._bespoke_audio_last_error()) });
      }
    } else if (data.type === 'sample' && data.pcm) {
      const pcm = data.pcm;
      const ptr = m._malloc(pcm.length * 4);
      m.HEAPF32.set(pcm, ptr >> 2);
      m._bespoke_audio_adopt_sample(data.id, data.channels, data.frames, data.rate, ptr);
      m._free(ptr);
    }
  }

  _atomicMaxFloat(index, value) {
    if (!(value > 0)) return;
    this._bitsF[0] = value;
    const bits = this._bitsI[0];
    let current = Atomics.load(this._statusI, index);
    while (bits > current) {
      const previous = Atomics.compareExchange(this._statusI, index, current, bits);
      if (previous === current) break;
      current = previous;
    }
  }

  _syncFlags(m) {
    const capture = Atomics.load(this._statusI, ST_MIDI_CAPTURE);
    if (capture !== this._midiCapture) {
      this._midiCapture = capture;
      m._bespoke_audio_set_midi_out_capture(capture);
    }
    const inputActive = Atomics.load(this._statusI, ST_INPUT_ACTIVE);
    if (inputActive !== this._inputActive) {
      this._inputActive = inputActive;
      m._bespoke_audio_set_input_active(inputActive);
    }
  }

  _drainEvents(m) {
    const ring = this._events;
    if (!ring) return;
    const capacity = Atomics.load(ring.header, RING_CAPACITY);
    let read = Atomics.load(ring.header, RING_READ);
    const write = Atomics.load(ring.header, RING_WRITE);
    let count = 0;
    while (read !== write && count < MAX_EVENTS_PER_QUANTUM) {
      const dst = count * EVENT_WORDS;
      for (let i = 0; i < EVENT_WORDS; ++i) this._eventView[dst + i] = ring.words[read + i];
      read += EVENT_WORDS;
      if (read >= capacity) read = 0;
      count += 1;
    }
    if (count === 0) return;
    Atomics.store(ring.header, RING_READ, read);
    m._bespoke_audio_apply_events(this._eventPtr, count);
  }

  _drainInput(m) {
    const ring = this._input;
    if (!ring) return;
    const capacity = Atomics.load(ring.header, RING_CAPACITY);
    let read = Atomics.load(ring.header, RING_READ);
    const write = Atomics.load(ring.header, RING_WRITE);
    let count = 0;
    while (read !== write && count < MAX_INPUT_PER_QUANTUM) {
      this._inputView[count++] = ring.floats[read];
      read += 1;
      if (read >= capacity) read = 0;
    }
    if (count === 0) return;
    Atomics.store(ring.header, RING_READ, read);
    m._bespoke_audio_push_input(this._inputPtr, count);
  }

  _drainMidiOut(m) {
    const ring = this._midiOut;
    if (!ring || !this._midiCapture) return;
    const header = ring.header;
    const capacity = Atomics.load(header, RING_CAPACITY);
    for (let round = 0; round < 8; ++round) {
      const got = m._bespoke_audio_drain_midi_out(this._midiPtr, MIDI_BATCH);
      let write = Atomics.load(header, RING_WRITE);
      for (let i = 0; i < got; ++i) {
        const next = write + 1 >= capacity ? 0 : write + 1;
        if (next === Atomics.load(header, RING_READ)) {
          Atomics.add(header, RING_DROPPED, got - i);
          break;
        }
        ring.words[write] = this._midiView[i];
        write = next;
      }
      Atomics.store(header, RING_WRITE, write);
      if (got < MIDI_BATCH) break;
    }
  }

  _publishAnalysis(left, right, frames) {
    const capacity = this._analysis.length;
    let total = Atomics.load(this._analysisHeader, 0);
    for (let i = 0; i < frames; ++i) {
      this._analysis[(total >>> 0) % capacity] = (left[i] + right[i]) * 0.5;
      total = (total + 1) | 0;
    }
    Atomics.store(this._analysisHeader, 0, total);
  }

  _publishStatus(m) {
    m._bespoke_audio_read_status(this._statusPtr, STATUS_SLOT_COUNT);
    const status = this._statusView;
    for (let i = 0; i < STATUS_SLOT_COUNT; ++i) this._statusD[i] = status[i];
    this._atomicMaxFloat(ST_PEAK_L, status[SLOT_PEAK_L]);
    this._atomicMaxFloat(ST_PEAK_R, status[SLOT_PEAK_R]);
    this._atomicMaxFloat(ST_PEAK_INPUT, status[SLOT_PEAK_INPUT]);
    Atomics.add(this._statusI, ST_SEQ, 1);
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!output || !output[0]) return true;
    const left = output[0];
    const right = output[1] || null;
    const frames = left.length;
    const m = this._m;
    if (!m || frames > MAX_BLOCK) {
      left.fill(0);
      if (right) right.fill(0);
      return true;
    }

    // A message handler may have grown the heap since the last quantum.
    if (m.HEAPU8.buffer !== this._heap || frames !== this._frames) this._refreshViews(frames);
    this._syncFlags(m);
    this._drainEvents(m);
    this._drainInput(m);
    m._bespoke_audio_process(this._left, this._right, frames);
    if (m.HEAPU8.buffer !== this._heap) this._refreshViews(frames);
    left.set(this._leftView);
    if (right) right.set(this._rightView);
    this._publishAnalysis(this._leftView, this._rightView, frames);
    this._drainMidiOut(m);
    this._publishStatus(m);
    return true;
  }
}

registerProcessor('bespoke-audio-engine', BespokeAudioEngineProcessor);
