import assert from 'node:assert/strict';
import { LookaheadController } from '../../src/audio/lookahead.ts';
import { resolveAudioBackendChoice, persistUrlAudioPreference } from '../../src/audio/audioMode.ts';

// --- Adaptive lookahead ---------------------------------------------------
{
  const la = new LookaheadController(1000, 20000, 2400, 0, 10000);
  assert.equal(la.target, 2400);
  assert.equal(la.update(0, 100), 2400, 'no underruns: target holds');

  assert.equal(la.update(128, 200), 3600, 'underrun grows 1.5x');
  assert.equal(la.floor, 3000, 'floor rises to 1.25x the starved level');
  assert.equal(la.update(128, 5000), 3600, 'no new underruns and < decay window: holds');
  assert.equal(la.update(128, 10200), 3150, 'clean for the decay window: decays 12.5%');
  assert.equal(la.update(128, 20200), 3000, 'decay clamps at the floor');
  assert.equal(la.update(128, 90000), 3000, 'never below the floor');

  assert.equal(la.update(5000, 90100, true), 3000, 'throttled (hidden tab) underruns are absorbed');
  assert.equal(la.update(5000, 90200), 3000, 'and do not count once visible again');

  const capped = new LookaheadController(1000, 4000, 3000, 0);
  capped.update(1, 1);
  assert.equal(capped.target, 4000, 'growth capped at max');
  assert.ok(capped.floor <= capped.target, 'floor never exceeds target');
}

// --- Backend resolution ---------------------------------------------------
const store = new Map<string, string>();
const g = globalThis as Record<string, unknown>;
g.window = {
  localStorage: {
    getItem: (k: string) => store.get(k) ?? null,
    setItem: (k: string, v: string) => void store.set(k, v),
  },
  location: { search: '' },
};

function setIsolation(on: boolean): void {
  g.crossOriginIsolated = on;
  g.AudioWorkletNode = on ? function AudioWorkletNode() {} : undefined;
  if (!on) delete g.AudioWorkletNode;
}

{
  setIsolation(true);
  let c = resolveAudioBackendChoice('');
  assert.deepEqual([c.backend, c.source, c.fallbackReason], ['worklet', 'auto', undefined], 'isolated: worklet by default');

  c = resolveAudioBackendChoice('?audio=sdl');
  assert.deepEqual([c.backend, c.source], ['sdl', 'url'], '?audio=sdl overrides');

  setIsolation(false);
  c = resolveAudioBackendChoice('');
  assert.equal(c.backend, 'sdl', 'not isolated: SDL2');
  assert.equal(c.requested, 'worklet');
  assert.match(c.fallbackReason ?? '', /cross-origin isolated|AudioWorkletNode/, 'explains why');

  c = resolveAudioBackendChoice('?audio=worklet');
  assert.equal(c.backend, 'sdl', 'explicit worklet still falls back without isolation');
  assert.ok(c.fallbackReason);

  setIsolation(true);
  persistUrlAudioPreference('?audio=sdl');
  c = resolveAudioBackendChoice('');
  assert.deepEqual([c.backend, c.source], ['sdl', 'stored'], 'URL override is persisted');
  c = resolveAudioBackendChoice('?audio=worklet');
  assert.deepEqual([c.backend, c.source], ['worklet', 'url'], 'URL beats stored preference');
}

console.log('audioMode/lookahead tests passed');
