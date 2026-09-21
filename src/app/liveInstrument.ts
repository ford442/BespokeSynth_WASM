import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { MidiClock, MidiOutputRouter } from '../midi';
import { QwertyPiano, type QwertyPianoState } from './qwertyPiano';

export function isMidiClockRequested(search: string = window.location.search): boolean {
  const value = new URLSearchParams(search).get('midiClock');
  return value === '1' || value === 'true';
}

/**
 * Local performance surface: QWERTY piano and script notes go to the WASM voice
 * path (same as hardware MIDI in) and are echoed to the selected MIDI output.
 */
export class LiveInstrument {
  readonly midiOut = new MidiOutputRouter();
  readonly piano: QwertyPiano;
  private clock: MidiClock | null = null;
  private readonly getModule: () => BespokeSynthModule | null;
  private readonly listeners = new Set<(state: QwertyPianoState) => void>();

  constructor(getModule: () => BespokeSynthModule | null) {
    this.getModule = getModule;
    this.piano = new QwertyPiano(
      {
        noteOn: (channel, pitch, velocity) => this.noteOn(channel, pitch, velocity),
        noteOff: (channel, pitch) => this.noteOff(channel, pitch),
      },
      (state) => this.listeners.forEach((listener) => listener(state)),
    );
  }

  onPianoChange(listener: (state: QwertyPianoState) => void): void {
    this.listeners.add(listener);
  }

  noteOn(channel: number, pitch: number, velocity: number): void {
    const ch = this.playChannel(channel);
    this.getModule()?._bespoke_midi_note_on?.(ch, pitch, velocity);
    this.midiOut.noteOn(ch, pitch, velocity);
  }

  noteOff(channel: number, pitch: number): void {
    const ch = this.playChannel(channel);
    this.getModule()?._bespoke_midi_note_off?.(ch, pitch);
    this.midiOut.noteOff(ch, pitch);
  }

  /** Local notes follow the graph's note-channel filter so they are never filtered out. */
  private playChannel(channel: number): number {
    const listen = this.getModule()?._bespoke_get_midi_note_channel?.() ?? -1;
    return listen >= 0 ? listen : channel;
  }

  /** Starts MIDI clock/transport out when ?midiClock=1. */
  enableClockIfRequested(): boolean {
    if (!isMidiClockRequested() || this.clock) return this.clock !== null;
    this.clock = new MidiClock(this.midiOut, {
      isPlaying: () => (this.getModule()?._bespoke_is_transport_playing?.() ?? 0) === 1,
      bpm: () => this.getModule()?._bespoke_get_tempo?.() ?? 120,
    });
    this.clock.start();
    return true;
  }

  isClockRunning(): boolean {
    return this.clock !== null;
  }
}
