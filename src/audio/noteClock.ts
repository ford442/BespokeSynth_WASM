/**
 * Stamps live notes (QWERTY, MIDI in) with the AudioContext time they should start at, so the
 * AudioWorklet engine plays them on their own frame (NoteMessage::timestamp) instead of at the
 * start of whichever quantum the event ring happened to reach. Played notes keep the spacing
 * they were played with; the cost is a small, steady delay (the margin).
 *
 * The event's performance.now() time is mapped onto the output timeline with
 * getOutputTimestamp(), then moved past what the engine has already rendered: `gap` is how far
 * currentTime runs ahead of what is heard (the largest seen lately, so the delay does not
 * wobble with quantum boundaries), and the margin covers the trip through the event ring plus
 * how long events wait before the page handles them (the largest wait lately, capped, decaying
 * slowly so the notes of a phrase keep their spacing). An event that waited longer than the cap
 * (a long frame on the main thread) cannot keep its spacing: it starts one quantum after
 * currentTime, the earliest the ring still reaches in time. A note the engine finds late anyway
 * is counted (lateNotes) and the ring margin grows; it decays back toward the floor while
 * nothing is late.
 */

export interface NoteClockContext {
  readonly currentTime: number;
  readonly sampleRate: number;
  getOutputTimestamp?(): AudioTimestamp;
}

/** About one render quantum: the ring is drained once per quantum. */
export const NOTE_MARGIN_FLOOR_MS = 3;
export const NOTE_MARGIN_MAX_MS = 25;
/** Most handling delay the margin absorbs; events that waited longer start as soon as possible. */
export const NOTE_WAIT_MAX_MS = 15;
const QUANTUM_FRAMES = 128;
/** How long a large gap is remembered before a smaller one replaces it. */
const GAP_WINDOW_MS = 2000;
/** Events older than this are clock trouble, not a performance: treated as just played. */
const MAX_EVENT_AGE_MS = 100;
const MARGIN_DECAY_MS_PER_S = 1;
const WAIT_DECAY_MS_PER_S = 2;

export class NoteClock {
  private readonly getContext: () => NoteClockContext | null;
  private readonly now: () => number;
  private gapMax = 0;
  private gapMaxAt = -Infinity;
  /** Ring margin: grows on late notes. */
  private marginMs = NOTE_MARGIN_FLOOR_MS;
  /** Largest recent handling delay of timestamped events, capped at NOTE_WAIT_MAX_MS. */
  private waitMs = 0;
  private waitAt = 0;
  private lastStamp = 0;
  private lastLate = -1;
  private lastObserved = 0;
  private context: NoteClockContext | null = null;

  constructor(getContext: () => NoteClockContext | null, now: () => number = () => performance.now()) {
    this.getContext = getContext;
    this.now = now;
  }

  /**
   * AudioContext seconds to start a note that happened at `eventTimeStamp` (performance.now()
   * milliseconds, e.g. Event.timeStamp), or 0 when no engine plays on that clock (play at once).
   * Stamps never go backwards, so notes keep the order they were played in.
   */
  stamp(eventTimeStamp?: number): number {
    const ctx = this.getContext();
    if (!ctx) {
      this.context = null;
      return 0;
    }
    if (ctx !== this.context) {
      // A new context starts its own timeline.
      this.context = ctx;
      this.lastStamp = 0;
      this.gapMax = 0;
      this.gapMaxAt = -Infinity;
    }
    const now = this.now();
    let event = now;
    if (eventTimeStamp !== undefined && Number.isFinite(eventTimeStamp)) {
      event = Math.min(now, Math.max(now - MAX_EVENT_AGE_MS, eventTimeStamp));
    }
    this.observeWait(now, now - event);
    const margin = this.getMarginMs() / 1000;
    const quantum = QUANTUM_FRAMES / ctx.sampleRate;

    let at: number;
    const output = ctx.getOutputTimestamp?.();
    const contextTime = output?.contextTime;
    const performanceTime = output?.performanceTime;
    if (contextTime !== undefined && performanceTime !== undefined && performanceTime > 0) {
      const heard = (time: number) => contextTime + (time - performanceTime) / 1000;
      const gap = Math.max(0, ctx.currentTime - heard(now));
      if (gap >= this.gapMax || now - this.gapMaxAt > GAP_WINDOW_MS) {
        this.gapMax = gap;
        this.gapMaxAt = now;
      }
      at = heard(event) + this.gapMax + margin;
    } else {
      // Not started yet (or no output timestamps): from the render clock, one quantum ahead.
      at = ctx.currentTime + quantum - (now - event) / 1000 + margin;
    }
    // Never before what the event ring can still reach in time.
    at = Math.max(at, ctx.currentTime + quantum);
    if (!(at > 0)) return 0;
    this.lastStamp = Math.max(this.lastStamp, at);
    return this.lastStamp;
  }

  /** Once per frame with the engine's lateNotes counter: late notes widen the margin. */
  observeLateNotes(total: number): void {
    const now = this.now();
    const elapsed = this.lastObserved > 0 ? Math.min(1, (now - this.lastObserved) / 1000) : 0;
    this.lastObserved = now;
    if (this.lastLate >= 0 && total > this.lastLate) {
      this.marginMs = Math.min(NOTE_MARGIN_MAX_MS, this.marginMs * 1.5 + 1);
    } else {
      this.marginMs = Math.max(NOTE_MARGIN_FLOOR_MS, this.marginMs - MARGIN_DECAY_MS_PER_S * elapsed);
    }
    this.lastLate = total;
  }

  /** Current delay added to live notes, milliseconds (HUD, tests). */
  getMarginMs(): number {
    return this.marginMs + this.waitMs;
  }

  private observeWait(now: number, waitMs: number): void {
    const elapsed = this.waitAt > 0 ? (now - this.waitAt) / 1000 : 0;
    this.waitAt = now;
    this.waitMs = Math.max(Math.min(waitMs, NOTE_WAIT_MAX_MS), this.waitMs - WAIT_DECAY_MS_PER_S * elapsed);
  }
}
