import type { BespokeSynthModule } from '../wasm/types/bespoke-synth';

interface MidiMessageEvent extends Event {
  data: Uint8Array;
}

interface MidiInput extends EventTarget {
  id: string;
  name?: string;
  onmidimessage: ((event: MidiMessageEvent) => void) | null;
}

export interface MidiOutputLike {
  id: string;
  name?: string;
  send(data: number[] | Uint8Array, timestamp?: number): void;
}

interface MidiAccess extends EventTarget {
  inputs: Map<string, MidiInput>;
  outputs?: Map<string, MidiOutputLike>;
}

type RequestMidiAccess = (options?: { sysex?: boolean }) => Promise<MidiAccess>;

export const MIDI_ACCESS_TIMEOUT_MS = 5000;

export type MidiConnectionState =
  | 'connected'
  | 'no-devices'
  | 'unsupported'
  | 'denied'
  | 'insecure'
  | 'timeout'
  | 'error';

export interface MidiInputInfo {
  id: string;
  name: string;
}

export interface MidiConnectResult {
  state: MidiConnectionState;
  inputs: MidiInputInfo[];
  outputs: MidiInputInfo[];
  connectedCount: number;
  message: string;
}

class MidiAccessTimeoutError extends Error {
  constructor() {
    super('MIDI access request timed out');
    this.name = 'MidiAccessTimeoutError';
  }
}

function withTimeout<T>(promise: Promise<T>, ms: number): Promise<T> {
  return new Promise((resolve, reject) => {
    const timer = window.setTimeout(() => reject(new MidiAccessTimeoutError()), ms);
    promise.then(
      (value) => {
        window.clearTimeout(timer);
        resolve(value);
      },
      (error: unknown) => {
        window.clearTimeout(timer);
        reject(error);
      },
    );
  });
}

function classifyMidiError(error: unknown): MidiConnectionState {
  if (error instanceof MidiAccessTimeoutError) return 'timeout';
  if (error instanceof DOMException) {
    if (error.name === 'NotAllowedError') return 'denied';
    if (error.name === 'SecurityError') return 'insecure';
  }
  return 'error';
}

function messageForState(state: MidiConnectionState, connectedCount = 0): string {
  switch (state) {
    case 'connected':
      return connectedCount === 1
        ? 'Connected to 1 MIDI input.'
        : `Connected to ${connectedCount} MIDI inputs.`;
    case 'no-devices':
      return 'No MIDI inputs detected. Connect a device and click MIDI again.';
    case 'unsupported':
      return 'Web MIDI is not supported in this browser.';
    case 'denied':
      return 'MIDI access was denied. Check site permissions in your browser settings.';
    case 'insecure':
      return 'Web MIDI requires a secure context (HTTPS or localhost).';
    case 'timeout':
      return 'MIDI access timed out. Try again or check browser permissions.';
    default:
      return 'Unable to connect to Web MIDI.';
  }
}

/** AudioContext seconds to start a note received at an event's timeStamp; 0 plays it at once. */
export type NoteTimeSource = (eventTimeStamp: number) => number;

/**
 * Routes one incoming MIDI message into the WASM graph. Note and CC messages
 * carry their channel; the graph applies the note-channel filter and CC learn.
 * `time` is when the note starts (NoteTimeSource), 0 for at once.
 */
export function routeMidiMessage(module: BespokeSynthModule, data: ArrayLike<number>, time = 0): void {
  // Realtime and other short messages carry fewer than three bytes.
  const status = data.length > 0 ? data[0] : 0;
  const data1 = data.length > 1 ? data[1] : 0;
  const data2 = data.length > 2 ? data[2] : 0;
  const command = status & 0xf0;
  const channel = status & 0x0f;
  if (command === 0x90 && data2 > 0) module._bespoke_midi_note_on(channel, data1, data2 / 127, time);
  else if (command === 0x80 || (command === 0x90 && data2 === 0)) module._bespoke_midi_note_off(channel, data1, time);
  else if (command === 0xb0) module._bespoke_midi_cc(channel, data1, data2 / 127);
}

function attachMidiHandlers(module: BespokeSynthModule, input: MidiInput, noteTime?: NoteTimeSource): void {
  input.onmidimessage = (event) => routeMidiMessage(module, event.data, noteTime?.(event.timeStamp) ?? 0);
}

let midiAccess: MidiAccess | null = null;

function listPorts<T extends { name?: string }>(ports: Map<string, T> | undefined, label: string): MidiInputInfo[] {
  const list: MidiInputInfo[] = [];
  ports?.forEach((port, id) => {
    list.push({ id, name: port.name?.trim() || `${label} ${list.length + 1}` });
  });
  return list;
}

export async function connectWebMidi(
  module: BespokeSynthModule,
  noteTime?: NoteTimeSource,
): Promise<MidiConnectResult> {
  // Absent in browsers without Web MIDI, whatever lib.dom says.
  const midiNavigator: { requestMIDIAccess?: RequestMidiAccess } = navigator as Navigator & {
    requestMIDIAccess?: RequestMidiAccess;
  };
  const request = midiNavigator.requestMIDIAccess;
  if (!request) {
    return {
      state: 'unsupported',
      inputs: [],
      outputs: [],
      connectedCount: 0,
      message: messageForState('unsupported'),
    };
  }

  if (!window.isSecureContext) {
    return {
      state: 'insecure',
      inputs: [],
      outputs: [],
      connectedCount: 0,
      message: messageForState('insecure'),
    };
  }

  let access: MidiAccess;
  try {
    // No SysEx: outputs only ever carry notes, transport, and clock.
    access = await withTimeout(request.call(navigator, { sysex: false }), MIDI_ACCESS_TIMEOUT_MS);
  } catch (error) {
    console.error('Web MIDI connection failed:', error);
    const state = classifyMidiError(error);
    return {
      state,
      inputs: [],
      outputs: [],
      connectedCount: 0,
      message: messageForState(state),
    };
  }

  midiAccess = access;
  access.inputs.forEach((input) => attachMidiHandlers(module, input, noteTime));
  // Hot-plugged inputs start routing without another click.
  access.addEventListener('statechange', () => {
    access.inputs.forEach((input) => {
      if (!input.onmidimessage) attachMidiHandlers(module, input, noteTime);
    });
  });

  const inputs = listPorts(access.inputs, 'MIDI input');
  const outputs = listPorts(access.outputs, 'MIDI output');
  const connectedCount = inputs.length;

  if (connectedCount === 0) {
    return {
      state: 'no-devices',
      inputs: [],
      outputs,
      connectedCount: 0,
      message: messageForState('no-devices'),
    };
  }

  return {
    state: 'connected',
    inputs,
    outputs,
    connectedCount,
    message: messageForState('connected', connectedCount),
  };
}

export interface MidiOutLogEntry {
  time: number;
  bytes: number[];
}

const MIDI_OUT_LOG_SIZE = 64;

/**
 * The selected Web MIDI output. Local performance notes (QWERTY piano, script
 * noteOn) are echoed here, plus transport start/stop and optional 24 PPQ clock.
 * Every message sent is kept in a small log for diagnostics and tests.
 */
export class MidiOutputRouter {
  private output: MidiOutputLike | null = null;
  private readonly log: MidiOutLogEntry[] = [];

  listOutputs(): MidiInputInfo[] {
    return listPorts(midiAccess?.outputs, 'MIDI output');
  }

  selectOutput(id: string | null): boolean {
    if (!id) {
      this.output = null;
      return true;
    }
    const port = midiAccess?.outputs?.get(id) ?? null;
    this.output = port;
    return port !== null;
  }

  selectedOutputId(): string | null {
    return this.output?.id ?? null;
  }

  hasOutput(): boolean {
    return this.output !== null;
  }

  send(bytes: number[], timestamp?: number): void {
    this.log.push({ time: timestamp ?? performance.now(), bytes: [...bytes] });
    if (this.log.length > MIDI_OUT_LOG_SIZE) this.log.shift();
    if (!this.output) return;
    try {
      this.output.send(bytes, timestamp);
    } catch (error) {
      console.warn('MIDI output send failed', error);
    }
  }

  noteOn(channel: number, pitch: number, velocity: number): void {
    const vel = Math.max(1, Math.min(127, Math.round(velocity * 127)));
    this.send([0x90 | (channel & 0x0f), pitch & 0x7f, vel]);
  }

  noteOff(channel: number, pitch: number): void {
    this.send([0x80 | (channel & 0x0f), pitch & 0x7f, 0]);
  }

  getLog(): MidiOutLogEntry[] {
    return this.log.map((entry) => ({ time: entry.time, bytes: [...entry.bytes] }));
  }

  clearLog(): void {
    this.log.length = 0;
  }
}

export interface TransportSource {
  isPlaying(): boolean;
  bpm(): number;
}

const CLOCK_PPQ = 24;
const CLOCK_TICK_MS = 25;
const CLOCK_LOOKAHEAD_MS = 100;

/**
 * MIDI clock out (?midiClock=1): Start/Stop on transport changes and 24 PPQ
 * ticks scheduled ahead with Web MIDI timestamps, so timer jitter does not
 * become clock jitter.
 */
export class MidiClock {
  private timer: number | null = null;
  private playing = false;
  private nextTickTime = 0;
  private readonly out: MidiOutputRouter;
  private readonly transport: TransportSource;

  constructor(out: MidiOutputRouter, transport: TransportSource) {
    this.out = out;
    this.transport = transport;
  }

  start(): void {
    if (this.timer !== null) return;
    this.timer = window.setInterval(() => this.pump(), CLOCK_TICK_MS);
  }

  stop(): void {
    if (this.timer !== null) window.clearInterval(this.timer);
    this.timer = null;
    if (this.playing) this.out.send([0xfc]);
    this.playing = false;
  }

  private pump(): void {
    const now = performance.now();
    const playing = this.transport.isPlaying();
    if (playing !== this.playing) {
      this.playing = playing;
      this.out.send([playing ? 0xfa : 0xfc], now);
      this.nextTickTime = now;
    }
    if (!playing) return;
    const bpm = Math.max(20, Math.min(400, this.transport.bpm() || 120));
    const interval = 60000 / (bpm * CLOCK_PPQ);
    if (this.nextTickTime < now - CLOCK_LOOKAHEAD_MS) this.nextTickTime = now; // tab was throttled
    while (this.nextTickTime < now + CLOCK_LOOKAHEAD_MS) {
      this.out.send([0xf8], this.nextTickTime);
      this.nextTickTime += interval;
    }
  }
}
