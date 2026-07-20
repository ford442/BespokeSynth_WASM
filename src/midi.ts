import type { BespokeSynthModule } from '../wasm/types/bespoke-synth';

interface MidiMessageEvent extends Event {
  data: Uint8Array;
}

interface MidiInput extends EventTarget {
  id: string;
  name?: string;
  onmidimessage: ((event: MidiMessageEvent) => void) | null;
}

interface MidiAccess {
  inputs: Map<string, MidiInput>;
}

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

function attachMidiHandlers(module: BespokeSynthModule, input: MidiInput): void {
  input.onmidimessage = (event) => {
    const [status, data1, data2] = event.data;
    const command = status & 0xf0;
    const channel = status & 0x0f;
    if (command === 0x90 && data2 > 0) module._bespoke_midi_note_on(channel, data1, data2 / 127);
    else if (command === 0x80 || (command === 0x90 && data2 === 0)) module._bespoke_midi_note_off(channel, data1);
    else if (command === 0xb0) module._bespoke_midi_cc(channel, data1, data2 / 127);
  };
}

export async function connectWebMidi(module: BespokeSynthModule): Promise<MidiConnectResult> {
  const request = (navigator as Navigator & { requestMIDIAccess?: () => Promise<MidiAccess> }).requestMIDIAccess;
  if (!request) {
    return {
      state: 'unsupported',
      inputs: [],
      connectedCount: 0,
      message: messageForState('unsupported'),
    };
  }

  if (!window.isSecureContext) {
    return {
      state: 'insecure',
      inputs: [],
      connectedCount: 0,
      message: messageForState('insecure'),
    };
  }

  let access: MidiAccess;
  try {
    access = await withTimeout(request.call(navigator), MIDI_ACCESS_TIMEOUT_MS);
  } catch (error) {
    console.error('Web MIDI connection failed:', error);
    const state = classifyMidiError(error);
    return {
      state,
      inputs: [],
      connectedCount: 0,
      message: messageForState(state),
    };
  }

  const inputs: MidiInputInfo[] = [];
  let connectedCount = 0;
  access.inputs.forEach((input, id) => {
    connectedCount++;
    inputs.push({
      id,
      name: input.name?.trim() || `MIDI input ${connectedCount}`,
    });
    attachMidiHandlers(module, input);
  });

  if (connectedCount === 0) {
    return {
      state: 'no-devices',
      inputs: [],
      connectedCount: 0,
      message: messageForState('no-devices'),
    };
  }

  return {
    state: 'connected',
    inputs,
    connectedCount,
    message: messageForState('connected', connectedCount),
  };
}
