/**
 * QWERTY piano, keyed by physical position (KeyboardEvent.code) so it works on
 * any layout and with Shift held. Values are semitones above the lower octave's C:
 * bottom row Z..M (+ S D G H J black keys) and top row Q..P (+ 2 3 5 6 7 9 0).
 * "/" is left out because it opens the spawn menu.
 */
export const QWERTY_PIANO_OFFSETS: Readonly<Record<string, number>> = Object.freeze({
  KeyZ: 0, KeyS: 1, KeyX: 2, KeyD: 3, KeyC: 4, KeyV: 5, KeyG: 6, KeyB: 7,
  KeyH: 8, KeyN: 9, KeyJ: 10, KeyM: 11, Comma: 12, KeyL: 13, Period: 14, Semicolon: 15,
  KeyQ: 12, Digit2: 13, KeyW: 14, Digit3: 15, KeyE: 16, KeyR: 17, Digit5: 18, KeyT: 19,
  Digit6: 20, KeyY: 21, Digit7: 22, KeyU: 23, KeyI: 24, Digit9: 25, KeyO: 26, Digit0: 27, KeyP: 28,
});

export function qwertyPianoOffset(code: string): number | undefined {
  return Object.prototype.hasOwnProperty.call(QWERTY_PIANO_OFFSETS, code) ? QWERTY_PIANO_OFFSETS[code] : undefined;
}

export interface NoteSink {
  noteOn(channel: number, pitch: number, velocity: number): void;
  noteOff(channel: number, pitch: number): void;
}

type PianoKeyEvent = Pick<KeyboardEvent, 'code' | 'repeat' | 'shiftKey' | 'ctrlKey' | 'metaKey' | 'altKey'>;

export interface QwertyPianoState {
  enabled: boolean;
  octave: number;
  velocity: number;
  channel: number;
}

/**
 * Computer-keyboard piano feeding the same voice path as hardware MIDI
 * (bespoke_midi_note_on/off), so Scale quantization and tunings apply.
 *
 * - Minus / Equal: octave down / up (C3..C7 base range)
 * - BracketLeft / BracketRight: default velocity -/+ 0.1
 * - Shift while pressing a key: full-velocity accent
 */
export class QwertyPiano {
  enabled = true;
  /** Octave of the bottom row's Z key; 4 means Z = C4 = MIDI 60. */
  octave = 4;
  velocity = 0.8;
  channel = 0;
  private readonly held = new Map<string, number>();
  private readonly sink: NoteSink;
  private readonly onChange?: (state: QwertyPianoState) => void;

  constructor(sink: NoteSink, onChange?: (state: QwertyPianoState) => void) {
    this.sink = sink;
    this.onChange = onChange;
  }

  getState(): QwertyPianoState {
    return { enabled: this.enabled, octave: this.octave, velocity: this.velocity, channel: this.channel };
  }

  setEnabled(enabled: boolean): void {
    if (!enabled) this.releaseAll();
    this.enabled = enabled;
    this.onChange?.(this.getState());
  }

  /** Returns true when the key belongs to the piano (caller should not forward it). */
  handleKeyDown(event: PianoKeyEvent): boolean {
    if (!this.enabled || event.ctrlKey || event.metaKey || event.altKey) return false;

    if (event.code === 'Minus' || event.code === 'Equal') {
      if (!event.repeat) this.shiftOctave(event.code === 'Minus' ? -1 : 1);
      return true;
    }
    if (event.code === 'BracketLeft' || event.code === 'BracketRight') {
      if (!event.repeat) {
        const delta = event.code === 'BracketLeft' ? -0.1 : 0.1;
        this.velocity = Math.round(Math.min(1, Math.max(0.1, this.velocity + delta)) * 10) / 10;
        this.onChange?.(this.getState());
      }
      return true;
    }

    const offset = qwertyPianoOffset(event.code);
    if (offset === undefined) return false;
    if (event.repeat || this.held.has(event.code)) return true;
    const pitch = (this.octave + 1) * 12 + offset;
    if (pitch < 0 || pitch > 127) return true;
    this.held.set(event.code, pitch);
    this.sink.noteOn(this.channel, pitch, event.shiftKey ? 1 : this.velocity);
    return true;
  }

  handleKeyUp(event: Pick<KeyboardEvent, 'code'>): boolean {
    const pitch = this.held.get(event.code);
    if (pitch !== undefined) {
      this.held.delete(event.code);
      this.sink.noteOff(this.channel, pitch);
      return true;
    }
    return this.enabled && (qwertyPianoOffset(event.code) !== undefined ||
      ['Minus', 'Equal', 'BracketLeft', 'BracketRight'].includes(event.code));
  }

  /** Note-off for everything held (window blur, piano disabled). */
  releaseAll(): void {
    for (const pitch of this.held.values()) this.sink.noteOff(this.channel, pitch);
    this.held.clear();
  }

  heldPitches(): number[] {
    return [...this.held.values()];
  }

  private shiftOctave(delta: number): void {
    // Held notes keep their pitch; their note-off uses the stored value.
    this.octave = Math.min(7, Math.max(0, this.octave + delta));
    this.onChange?.(this.getState());
  }
}
