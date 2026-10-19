import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

/** Where decoded graph notes go; MidiOutputRouter satisfies it. */
export interface GraphNoteSink {
  hasOutput(): boolean;
  send(bytes: number[], timestamp?: number): void;
}

/** Graph notes from an engine outside the UI module (the AudioWorklet engine's shared ring). */
export interface GraphNoteSource {
  setCapture(enabled: boolean): void;
  /** Copies packed notes (the bespoke_drain_graph_notes packing) into `out`; returns the count. */
  drain(out: Int32Array): number;
}

export interface GraphNote {
  pitch: number;
  /** 0-127 */
  velocity: number;
  channel: number;
  on: boolean;
}

/** Events per drain call; matches the stack buffer in bespoke_drain_graph_notes. */
export const GRAPH_NOTE_BATCH = 64;
/** The engine ring holds 512 notes, so a drain loop never needs more rounds than this. */
const MAX_ROUNDS = 8;
const POLL_MS = 8;

/** Inverse of the packing in bespoke_drain_graph_notes: pitch | velocity << 8 | channel << 16 | on << 20. */
export function unpackGraphNote(packed: number): GraphNote {
  return {
    pitch: packed & 0x7f,
    velocity: (packed >> 8) & 0xff,
    channel: (packed >> 16) & 0x0f,
    on: ((packed >> 20) & 1) === 1,
  };
}

export function graphNoteBytes(note: GraphNote): number[] {
  if (note.on) return [0x90 | note.channel, note.pitch, Math.max(1, Math.min(127, note.velocity))];
  return [0x80 | note.channel, note.pitch, 0];
}

export interface GraphNotePumpOptions {
  /** How long after "now" the audible result of a drained note lands (queued audio ahead of the speakers). */
  latencyMs?: () => number;
  now?: () => number;
}

/**
 * Forwards the notes the audio graph plays (sequencers, drum machine, scripts, arpeggiator, chorder...)
 * to the selected MIDI output. The engine only records them while a port is selected, so with no output
 * this costs one poll of a boolean. Performance notes (QWERTY, MIDI in) are echoed by LiveInstrument
 * instead; the engine never reports those, which is what keeps a note from going out twice.
 */
export class GraphNotePump {
  private timer: ReturnType<typeof setInterval> | null = null;
  private module: BespokeSynthModule | null = null;
  private bufferPtr = 0;
  private capturing = false;
  /** The AudioWorklet engine's notes, when it (not the UI module) plays the graph. */
  private source: GraphNoteSource | null = null;
  private readonly sourceBuffer = new Int32Array(GRAPH_NOTE_BATCH);
  private latency: () => number;
  private readonly now: () => number;
  private readonly getModule: () => BespokeSynthModule | null;
  private readonly sink: GraphNoteSink;

  constructor(getModule: () => BespokeSynthModule | null, sink: GraphNoteSink, options: GraphNotePumpOptions = {}) {
    this.getModule = getModule;
    this.sink = sink;
    this.latency = options.latencyMs ?? (() => 0);
    this.now = options.now ?? (() => performance.now());
  }

  setLatencySource(latencyMs: () => number): void {
    this.latency = latencyMs;
  }

  start(): void {
    if (this.timer !== null) return;
    this.timer = setInterval(() => this.tick(), POLL_MS);
  }

  stop(): void {
    if (this.timer !== null) clearInterval(this.timer);
    this.timer = null;
    this.release();
  }

  isCapturing(): boolean {
    return this.capturing;
  }

  /** Switches to (or, with null, away from) a worklet engine's note ring. */
  setSource(source: GraphNoteSource | null): void {
    if (source === this.source) return;
    this.release();
    this.module = null;
    this.source = source;
    // Capture now, not on the next poll: notes the engine plays before then (Play right after the
    // switch) would otherwise never reach the port.
    if (source && this.sink.hasOutput()) {
      source.setCapture(true);
      this.capturing = true;
    }
  }

  /** One poll: follows the port selection, then sends whatever the engine queued. Returns the count sent. */
  tick(): number {
    if (this.source) return this.tickSource(this.source);
    const module = this.getModule();
    if (!module?._bespoke_drain_graph_notes || !module._bespoke_set_graph_note_capture) return 0;
    if (module !== this.module) {
      // A fresh module instance knows nothing of our buffer or capture request.
      this.module = module;
      this.bufferPtr = 0;
      this.capturing = false;
    }

    const wanted = this.sink.hasOutput();
    if (wanted !== this.capturing) {
      module._bespoke_set_graph_note_capture(wanted ? 1 : 0);
      this.capturing = wanted;
    }
    if (!wanted) return 0;

    if (!this.bufferPtr) this.bufferPtr = module._malloc(GRAPH_NOTE_BATCH * 4);
    const timestamp = this.now() + Math.max(0, this.latency());
    let sent = 0;
    for (let round = 0; round < MAX_ROUNDS; round += 1) {
      const got = module._bespoke_drain_graph_notes(this.bufferPtr, GRAPH_NOTE_BATCH);
      // The build exports HEAPU8 but not HEAP32, and growth replaces the buffer, so view it after the call.
      const packed = new Int32Array(module.HEAPU8.buffer, this.bufferPtr, got);
      for (let i = 0; i < got; i += 1) {
        this.sink.send(graphNoteBytes(unpackGraphNote(packed[i])), timestamp);
        sent += 1;
      }
      if (got < GRAPH_NOTE_BATCH) break;
    }
    return sent;
  }

  private tickSource(source: GraphNoteSource): number {
    const wanted = this.sink.hasOutput();
    if (wanted !== this.capturing) {
      source.setCapture(wanted);
      this.capturing = wanted;
    }
    if (!wanted) return 0;
    const timestamp = this.now() + Math.max(0, this.latency());
    let sent = 0;
    for (let round = 0; round < MAX_ROUNDS; round += 1) {
      const got = source.drain(this.sourceBuffer);
      for (let i = 0; i < got; i += 1) {
        this.sink.send(graphNoteBytes(unpackGraphNote(this.sourceBuffer[i])), timestamp);
        sent += 1;
      }
      if (got < GRAPH_NOTE_BATCH) break;
    }
    return sent;
  }

  private release(): void {
    if (this.source && this.capturing) this.source.setCapture(false);
    if (this.module && this.capturing) this.module._bespoke_set_graph_note_capture?.(0);
    if (this.module && this.bufferPtr) this.module._free(this.bufferPtr);
    this.capturing = false;
    this.bufferPtr = 0;
  }
}
