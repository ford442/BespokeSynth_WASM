import assert from 'node:assert/strict';
import {
  NOTE_MARGIN_FLOOR_MS,
  NOTE_MARGIN_MAX_MS,
  NOTE_WAIT_MAX_MS,
  NoteClock,
  type NoteClockContext,
} from '../../src/audio/noteClock.ts';

/**
 * A context whose output lags its render clock by `lagMs` (base + output latency, plus the
 * quantum already rendered). Time only moves when the test says so.
 */
class FakeContext implements NoteClockContext {
  readonly sampleRate = 48000;
  perfNow = 10_000;
  started = true;
  lagMs: number;
  private readonly startPerf: number;
  constructor(startPerf: number, lagMs: number) {
    this.startPerf = startPerf;
    this.lagMs = lagMs;
  }
  get currentTime(): number {
    return (this.perfNow - this.startPerf + this.lagMs) / 1000;
  }
  getOutputTimestamp(): AudioTimestamp {
    if (!this.started) return { contextTime: 0, performanceTime: 0 };
    // What is heard right now: the render clock minus the lag.
    return { contextTime: (this.perfNow - this.startPerf) / 1000, performanceTime: this.perfNow };
  }
}

const QUANTUM_S = 128 / 48000;
const close = (a: number, b: number, eps = 1e-9) => Math.abs(a - b) <= eps;
const rig = (lagMs = 20) => {
  const ctx = new FakeContext(9_000, lagMs);
  let live: NoteClockContext | null = ctx;
  const clock = new NoteClock(
    () => live,
    () => ctx.perfNow,
  );
  return { ctx, clock, unplug: () => (live = null) };
};

// A note handled the moment it was played starts the floor margin past what the engine has
// rendered (currentTime), and notes handled 7.3 ms apart start 7.3 ms apart.
{
  const { ctx, clock } = rig();
  const first = clock.stamp(ctx.perfNow);
  assert.ok(close(first, ctx.currentTime + NOTE_MARGIN_FLOOR_MS / 1000), `first stamp ${first}`);
  ctx.perfNow += 50;
  const a = clock.stamp(ctx.perfNow);
  ctx.perfNow += 7.3;
  const b = clock.stamp(ctx.perfNow);
  assert.ok(close(b - a, 0.0073), `spacing ${b - a}`);
  // Stamps never go backwards (an event that waited too long to keep its place).
  assert.equal(clock.stamp(ctx.perfNow - 50), b);
}

// The margin takes in how long events wait to be handled, so events that waited keep their
// spacing; one that waited longer than the cap (a long main-thread frame) starts one quantum
// past currentTime. The wait part decays once events are handled promptly again.
{
  const { ctx, clock } = rig();
  ctx.perfNow += 30;
  clock.stamp(ctx.perfNow - 6);
  assert.ok(close(clock.getMarginMs(), NOTE_MARGIN_FLOOR_MS + 6), `learned margin ${clock.getMarginMs()}`);
  ctx.perfNow += 30;
  const early = clock.stamp(ctx.perfNow - 5);
  const later = clock.stamp(ctx.perfNow - 1);
  assert.ok(close(later - early, 0.004), `waiting events keep their spacing (${later - early})`);
  assert.ok(close(early, ctx.currentTime - 0.005 + clock.getMarginMs() / 1000), `early ${early}`);
  ctx.perfNow += 30;
  const stuck = clock.stamp(ctx.perfNow - 40);
  assert.ok(close(stuck, ctx.currentTime + QUANTUM_S), `a 40 ms wait starts as soon as possible (${stuck})`);
  assert.equal(clock.getMarginMs(), NOTE_MARGIN_FLOOR_MS + NOTE_WAIT_MAX_MS, 'handling delay is capped');
  ctx.perfNow += 1000;
  clock.stamp(ctx.perfNow);
  assert.ok(close(clock.getMarginMs(), NOTE_MARGIN_FLOOR_MS + NOTE_WAIT_MAX_MS - 2), `decays ${clock.getMarginMs()}`);
  ctx.perfNow += 10_000;
  clock.stamp(ctx.perfNow);
  assert.equal(clock.getMarginMs(), NOTE_MARGIN_FLOOR_MS, 'back to the floor');
  // Stale timestamps (clock trouble) count as at most 100 ms old and never land in the past.
  ctx.perfNow += 30;
  const stale = clock.stamp(ctx.perfNow - 5000);
  assert.ok(close(stale, ctx.currentTime + QUANTUM_S), `stale ${stale}`);
}

// The largest recent gap is kept, so a shrinking gap does not pull notes earlier, until the
// window passes.
{
  const { ctx, clock } = rig(20);
  clock.stamp();
  ctx.perfNow += 100;
  ctx.lagMs = 10;
  const kept = clock.stamp();
  assert.ok(close(kept, ctx.currentTime + 0.01 + NOTE_MARGIN_FLOOR_MS / 1000), `kept gap ${kept}`);
  ctx.perfNow += 2500;
  const relaxed = clock.stamp();
  assert.ok(close(relaxed, ctx.currentTime + NOTE_MARGIN_FLOOR_MS / 1000), `relaxed ${relaxed}`);
}

// Late notes reported by the engine grow the margin up to the cap; it decays while none are late.
{
  const { ctx, clock } = rig();
  clock.observeLateNotes(0);
  ctx.perfNow += 16;
  clock.observeLateNotes(1);
  const grown = clock.getMarginMs();
  assert.ok(grown > NOTE_MARGIN_FLOOR_MS, `margin grows on a late note (${grown})`);
  for (let i = 2; i < 40; i += 1) {
    ctx.perfNow += 16;
    clock.observeLateNotes(i);
  }
  assert.equal(clock.getMarginMs(), NOTE_MARGIN_MAX_MS, 'margin is capped');
  // A wider margin also keeps the spacing of events that waited to be handled.
  ctx.perfNow += 100;
  const late1 = clock.stamp(ctx.perfNow - 9);
  const late2 = clock.stamp(ctx.perfNow - 1.7);
  assert.ok(close(late2 - late1, 0.0073), `spacing of waiting events ${late2 - late1}`);
  for (let i = 0; i < 60 * 30; i += 1) {
    ctx.perfNow += 16;
    clock.observeLateNotes(39);
  }
  clock.stamp(ctx.perfNow);
  assert.equal(clock.getMarginMs(), NOTE_MARGIN_FLOOR_MS, 'margin decays to the floor');
}

// Before output timestamps exist: one quantum past the render clock, plus the margin.
{
  const { ctx, clock } = rig();
  ctx.started = false;
  assert.ok(close(clock.stamp(), ctx.currentTime + QUANTUM_S + NOTE_MARGIN_FLOOR_MS / 1000));
}

// No engine on an AudioContext clock: 0, the engine plays the note at once.
{
  const { ctx, clock, unplug } = rig();
  unplug();
  assert.equal(clock.stamp(ctx.perfNow), 0);
}

console.log('noteClock.test.ts passed');
