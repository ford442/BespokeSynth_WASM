import assert from 'node:assert/strict';
import {
  AudioStatusMonitor,
  evaluateAudioStatus,
  type AudioStatusInput,
} from '../../src/audio/audioStatus.ts';

const base: AudioStatusInput = {
  backend: 'port',
  playRequested: false,
  startError: null,
  deviceError: null,
  contextState: null,
  clockAdvancing: null,
  outputDevices: null,
  sampleRate: null,
  latencyMs: null,
};
const verdict = (patch: Partial<AudioStatusInput>) => evaluateAudioStatus({ ...base, ...patch });

// --- Verdicts -------------------------------------------------------------
{
  let s = verdict({});
  assert.deepEqual([s.level, s.state], ['idle', 'idle'], 'nothing created yet: idle, not "running"');

  s = verdict({ playRequested: true });
  assert.equal(s.state, 'starting');

  s = verdict({ outputDevices: 0 });
  assert.deepEqual([s.level, s.state], ['warn', 'no-device'], 'no device is visible before Play');
  s = verdict({ outputDevices: 2 });
  assert.equal(s.state, 'idle', 'devices listed: nothing to warn about');
  s = verdict({ outputDevices: null });
  assert.equal(s.state, 'idle', 'browser does not list outputs: unknown is not a warning');

  s = verdict({ contextState: 'suspended' });
  assert.equal(s.state, 'idle', 'suspended before Play is the normal autoplay state');
  s = verdict({ contextState: 'suspended', playRequested: true });
  assert.deepEqual([s.level, s.state], ['warn', 'suspended'], 'suspended after Play is a problem');

  s = verdict({ contextState: 'running', clockAdvancing: true, sampleRate: 48000, latencyMs: 11.6 });
  assert.deepEqual([s.level, s.state], ['ok', 'running']);
  assert.match(s.message, /48 kHz/);
  assert.match(s.message, /12 ms/);
  s = verdict({ contextState: 'running', sampleRate: 44100 });
  assert.match(s.message, /44\.1 kHz/);
  assert.equal(s.state, 'running', 'clock not measured yet: trust the context');

  s = verdict({ contextState: 'running', clockAdvancing: false });
  assert.deepEqual([s.level, s.state], ['error', 'stalled'], 'a running context with a frozen clock has no device');

  s = verdict({ contextState: 'running', clockAdvancing: true, outputDevices: 0 });
  assert.deepEqual([s.level, s.state], ['error', 'no-device'], 'running with zero outputs listed is not "ok"');

  s = verdict({ contextState: 'closed' });
  assert.equal(s.state, 'closed');
  s = verdict({ contextState: 'interrupted' });
  assert.equal(s.state, 'interrupted');

  s = verdict({ contextState: 'running', startError: 'boom' });
  assert.deepEqual([s.level, s.state], ['error', 'failed'], 'a start failure wins over a running context');
  assert.match(s.message, /boom/);
  s = verdict({ deviceError: 'device lost' });
  assert.equal(s.state, 'failed');
}

// --- Monitor against a fake AudioContext ------------------------------------
class FakeContext extends EventTarget {
  state: AudioContextState = 'running';
  currentTime = 0;
  sampleRate = 48000;
  baseLatency = 0.005;
  outputLatency = 0.01;
  setState(state: AudioContextState): void {
    this.state = state;
    this.dispatchEvent(new Event('statechange'));
  }
}

{
  const ctx = new FakeContext();
  let current: FakeContext | null = null;
  let playing = false;
  const monitor = new AudioStatusMonitor({
    getBackend: () => 'port',
    getContext: () => current as unknown as AudioContext | null,
    getPlayRequested: () => playing,
  });
  const seen: string[] = [];
  monitor.subscribe((s) => seen.push(s.state));
  assert.deepEqual(seen, ['idle'], 'subscribers get the current status at once');

  current = ctx;
  playing = true;
  let t = 1000;
  assert.equal(monitor.poll(t).state, 'running', 'first sight of a running context');

  // The clock moves with real time: stays ok.
  ctx.currentTime += 1;
  t += 1000;
  let s = monitor.poll(t);
  assert.equal(s.clockAdvancing, true);
  assert.equal(s.level, 'ok');

  // The clock freezes (no device pulling samples): stalled after a full window.
  t += 1000;
  s = monitor.poll(t);
  assert.equal(s.clockAdvancing, false);
  assert.deepEqual([s.level, s.state], ['error', 'stalled']);

  // Recovery.
  ctx.currentTime += 1;
  t += 1000;
  assert.equal(monitor.poll(t).state, 'running');

  // statechange events republish without waiting for a poll.
  ctx.setState('suspended');
  assert.equal(seen.at(-1), 'suspended');
  ctx.setState('running');

  // Context error event.
  ctx.dispatchEvent(new Event('error'));
  assert.equal(monitor.getStatus().state, 'failed');

  // A new context clears the old verdict.
  const next = new FakeContext();
  current = next;
  assert.equal(monitor.poll(t + 5000).state, 'running');

  monitor.noteDeviceError('The AudioContext could not be created');
  assert.equal(monitor.getStatus().state, 'failed');
  monitor.noteStartError(null);
  assert.equal(monitor.getStatus().state, 'running', 'a new Play attempt clears the old device error');
  monitor.noteDeviceError('again');
  current = new FakeContext();
  assert.equal(monitor.poll(t + 9000).state, 'running', 'a new context supersedes the device error');

  monitor.noteStartError('no worklet');
  assert.equal(monitor.getStatus().state, 'failed');
  monitor.noteStartError(null);
  assert.equal(monitor.getStatus().state, 'running');
  monitor.dispose();
}

console.log('audioStatus tests passed');
