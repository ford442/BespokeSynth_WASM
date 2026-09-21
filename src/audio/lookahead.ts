/**
 * Adaptive producer lookahead. The main thread fills the ring only up to a
 * target depth (lower latency than filling it to capacity); underruns grow the
 * target, and long clean stretches let it decay back toward the floor.
 */
export class LookaheadController {
  target: number;
  floor: number;
  private readonly maxFrames: number;
  private readonly decayAfterMs: number;
  private lastUnderruns = 0;
  private cleanSinceMs: number;

  constructor(minFrames: number, maxFrames: number, initialFrames: number, nowMs: number, decayAfterMs = 10000) {
    this.maxFrames = maxFrames;
    this.decayAfterMs = decayAfterMs;
    this.target = Math.min(maxFrames, Math.max(minFrames, initialFrames));
    this.floor = minFrames;
    this.cleanSinceMs = nowMs;
  }

  /**
   * Feed the worklet's cumulative underrun counter; returns the new target.
   * `throttled` (hidden tab: timers clamp to ~1 s) absorbs underruns without
   * growing, so a backgrounded tab does not ratchet latency up for good.
   */
  update(underruns: number, nowMs: number, throttled = false): number {
    if (throttled) {
      this.lastUnderruns = underruns;
      this.cleanSinceMs = nowMs;
      return this.target;
    }
    if (underruns > this.lastUnderruns) {
      // The current target was not enough: grow 1.5x, and never decay back to
      // within 25% of the level that just starved.
      const failed = this.target;
      this.target = Math.min(this.maxFrames, Math.ceil(failed * 1.5));
      this.floor = Math.min(this.target, Math.max(this.floor, Math.ceil(failed * 1.25)));
      this.cleanSinceMs = nowMs;
    } else if (nowMs - this.cleanSinceMs >= this.decayAfterMs && this.target > this.floor) {
      this.target = Math.max(this.floor, Math.floor(this.target * 0.875));
      this.cleanSinceMs = nowMs;
    }
    this.lastUnderruns = underruns;
    return this.target;
  }
}
