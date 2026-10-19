/** Where echoed performance notes go; MidiOutputRouter satisfies it. */
export interface NoteEchoSink {
  noteOn(channel: number, pitch: number, velocity: number): void;
  noteOff(channel: number, pitch: number): void;
}

/**
 * Echoes performance notes (QWERTY, or a script note with no cable) to the MIDI output as played.
 *
 * When a Note processor with no cable is listening to the performance (chorder, arp...), the engine
 * reports that processor's output instead, so the raw key is held back to avoid sending it twice.
 * A note-on that did go out raw is always paired with its note-off, even if a processor appears
 * while the key is held, so the receiving device is never left with a stuck note.
 */
export class PerformanceEcho {
  private readonly sink: NoteEchoSink;
  private readonly isProcessed: () => boolean;
  private readonly echoedRaw = new Set<number>();

  constructor(sink: NoteEchoSink, isProcessed: () => boolean) {
    this.sink = sink;
    this.isProcessed = isProcessed;
  }

  echo(on: boolean, channel: number, pitch: number, velocity: number): void {
    const key = (channel & 0x0f) * 128 + (pitch & 0x7f);
    if (on) {
      if (this.isProcessed()) return;
      this.echoedRaw.add(key);
      this.sink.noteOn(channel, pitch, velocity);
    } else if (this.echoedRaw.delete(key)) {
      this.sink.noteOff(channel, pitch);
    }
  }
}
