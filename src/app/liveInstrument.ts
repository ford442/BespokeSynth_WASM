import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { NoteClock, type NoteClockContext } from '../audio/noteClock';
import { MidiClock, MidiOutputRouter } from '../midi';
import { GraphNotePump } from './graphMidiOut';
import { PerformanceEcho } from './performanceEcho';
import { QwertyPiano, type QwertyPianoState } from './qwertyPiano';

export function isMidiClockRequested(search: string = window.location.search): boolean {
  const value = new URLSearchParams(search).get('midiClock');
  return value === '1' || value === 'true';
}

/**
 * Local performance surface: QWERTY piano and script notes go to the WASM voice
 * path (same as hardware MIDI in) and are echoed to the selected MIDI output.
 * Notes the graph itself plays (sequencers, drums, arp...) reach the same output
 * through the GraphNotePump, so the two paths together cover everything audible.
 */
export class LiveInstrument {
  readonly midiOut = new MidiOutputRouter();
  readonly graphNotes: GraphNotePump;
  readonly piano: QwertyPiano;
  private clock: MidiClock | null = null;
  private audioClock: () => NoteClockContext | null = () => null;
  /** Start times for QWERTY and MIDI-in notes on the AudioWorklet engine's clock. */
  readonly noteClock = new NoteClock(() => this.audioClock());
  private readonly echo: PerformanceEcho;
  private readonly getModule: () => BespokeSynthModule | null;
  private readonly listeners = new Set<(state: QwertyPianoState) => void>();

  constructor(getModule: () => BespokeSynthModule | null) {
    this.getModule = getModule;
    this.graphNotes = new GraphNotePump(getModule, this.midiOut);
    this.echo = new PerformanceEcho(
      this.midiOut,
      () => (getModule()?._bespoke_performance_notes_processed() ?? 0) === 1,
    );
    this.piano = new QwertyPiano(
      {
        noteOn: (channel, pitch, velocity, eventTime) => this.noteOn(channel, pitch, velocity, eventTime),
        noteOff: (channel, pitch, eventTime) => this.noteOff(channel, pitch, eventTime),
      },
      (state) => this.listeners.forEach((listener) => listener(state)),
    );
  }

  onPianoChange(listener: (state: QwertyPianoState) => void): void {
    this.listeners.add(listener);
  }

  /** `eventTime`: when it was played (Event.timeStamp); the engine starts it that far apart from the others. */
  noteOn(channel: number, pitch: number, velocity: number, eventTime?: number): void {
    const ch = this.playChannel(channel);
    this.getModule()?._bespoke_midi_note_on(ch, pitch, velocity, this.noteTime(eventTime));
    this.echoPerformanceNote(true, ch, pitch, velocity);
  }

  noteOff(channel: number, pitch: number, eventTime?: number): void {
    const ch = this.playChannel(channel);
    this.getModule()?._bespoke_midi_note_off(ch, pitch, this.noteTime(eventTime));
    this.echoPerformanceNote(false, ch, pitch, 0);
  }

  /** AudioContext seconds to start a note played at `eventTime`, or 0 (at once) without the AudioWorklet engine. */
  noteTime(eventTime?: number): number {
    return this.noteClock.stamp(eventTime);
  }

  /** The AudioWorklet engine's context while it plays (null otherwise: notes are not timed). */
  setAudioClock(getContext: () => NoteClockContext | null): void {
    this.audioClock = getContext;
  }

  /**
   * Sends a performance note (QWERTY, or a script note with no cable) to the MIDI output as played.
   * Held back when a Note processor owns the performance: the graph reports its output instead.
   */
  echoPerformanceNote(on: boolean, channel: number, pitch: number, velocity: number): void {
    this.echo.echo(on, channel, pitch, velocity);
  }

  /** Delay graph notes by the audio still queued ahead of the speakers, so MIDI lands with the sound. */
  setGraphNoteLatency(latencyMs: () => number): void {
    this.graphNotes.setLatencySource(latencyMs);
  }

  /** Local notes follow the graph's note-channel filter so they are never filtered out. */
  private playChannel(channel: number): number {
    const listen = this.getModule()?._bespoke_get_midi_note_channel() ?? -1;
    return listen >= 0 ? listen : channel;
  }

  /** Starts MIDI clock/transport out when ?midiClock=1. */
  enableClockIfRequested(): boolean {
    if (!isMidiClockRequested() || this.clock) return this.clock !== null;
    this.clock = new MidiClock(this.midiOut, {
      isPlaying: () => (this.getModule()?._bespoke_is_transport_playing() ?? 0) === 1,
      bpm: () => this.getModule()?._bespoke_get_tempo() ?? 120,
    });
    this.clock.start();
    return true;
  }

  /** Starts forwarding graph notes to the selected MIDI output (cheap while no port is selected). */
  startGraphNotes(): void {
    this.graphNotes.start();
  }

  isClockRunning(): boolean {
    return this.clock !== null;
  }
}
