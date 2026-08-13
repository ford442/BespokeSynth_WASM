/**
 * Opt-in AudioWorklet isolation POC (?audioWorkletPoc=1).
 * Plays a continuous tone in the worklet, then busy-waits the main thread
 * for stallMs and verifies frame counters kept advancing.
 */

export interface WorkletPocResult {
  ok: boolean;
  reason?: string;
  framesBeforeStall: number;
  framesAfterStall: number;
  framesDelta: number;
  stallMs: number;
  expectedMinDelta: number;
}

function workletUrl(): string {
  // Served from webpack CopyPlugin → dist/audio/
  return new URL('audio/tonePoc.worklet.js', window.location.href).toString();
}

export async function runAudioWorkletPoc(options: {
  stallMs?: number;
  settleMs?: number;
} = {}): Promise<WorkletPocResult> {
  const stallMs = options.stallMs ?? 100;
  const settleMs = options.settleMs ?? 200;

  if (typeof AudioWorkletNode === 'undefined') {
    return {
      ok: false,
      reason: 'AudioWorkletNode unavailable',
      framesBeforeStall: 0,
      framesAfterStall: 0,
      framesDelta: 0,
      stallMs,
      expectedMinDelta: 0,
    };
  }

  const ctx = new AudioContext();
  let frames = 0;
  try {
    await ctx.audioWorklet.addModule(workletUrl());
    const node = new AudioWorkletNode(ctx, 'bespoke-tone-poc');
    node.port.onmessage = (ev) => {
      if (ev.data?.type === 'frames' && typeof ev.data.frames === 'number') {
        frames = ev.data.frames;
      }
    };
    node.connect(ctx.destination);
    if (ctx.state === 'suspended') await ctx.resume();

    await new Promise((r) => setTimeout(r, settleMs));
    const framesBeforeStall = frames;

    const stallUntil = performance.now() + stallMs;
    while (performance.now() < stallUntil) {
      // Deliberate main-thread busy wait (POC isolation proof).
    }

    await new Promise((r) => setTimeout(r, settleMs));
    const framesAfterStall = frames;
    const framesDelta = framesAfterStall - framesBeforeStall;
    // During stall + settle, audio thread should keep producing.
    // Expect at least ~half of stall duration worth of frames at ctx.sampleRate.
    const expectedMinDelta = Math.floor((ctx.sampleRate * stallMs) / 1000 / 2);

    node.port.postMessage({ type: 'stop' });
    node.disconnect();
    await ctx.close();

    return {
      ok: framesDelta >= expectedMinDelta,
      reason: framesDelta >= expectedMinDelta ? undefined : 'insufficient frames during stall',
      framesBeforeStall,
      framesAfterStall,
      framesDelta,
      stallMs,
      expectedMinDelta,
    };
  } catch (err) {
    try {
      await ctx.close();
    } catch {
      // ignore
    }
    return {
      ok: false,
      reason: err instanceof Error ? err.message : String(err),
      framesBeforeStall: 0,
      framesAfterStall: 0,
      framesDelta: 0,
      stallMs,
      expectedMinDelta: 0,
    };
  }
}
