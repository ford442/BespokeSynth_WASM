import assert from 'node:assert/strict';
import { DEFAULT_SAMPLE_RATE, probeAudioDevice } from '../../src/audio/deviceProbe.ts';

const g = globalThis as Record<string, unknown>;
let closed = 0;

class OkContext {
  sampleRate = 48000;
  close(): Promise<void> {
    closed += 1;
    return Promise.resolve();
  }
}
g.AudioContext = OkContext;
let probe = probeAudioDevice();
assert.deepEqual(probe, { sampleRate: 48000, error: null }, 'reads the device rate');
assert.equal(closed, 1, 'the probe context is closed again at once');

g.AudioContext = class {
  constructor() {
    throw new Error('no audio stack');
  }
};
probe = probeAudioDevice();
assert.equal(probe.sampleRate, DEFAULT_SAMPLE_RATE, 'falls back to 44.1 kHz');
assert.equal(probe.error, 'no audio stack', 'and reports why');

g.AudioContext = class {
  sampleRate = 0;
  close(): Promise<void> {
    return Promise.resolve();
  }
};
probe = probeAudioDevice();
assert.deepEqual(probe, { sampleRate: DEFAULT_SAMPLE_RATE, error: null }, 'an implausible rate is not trusted');

delete g.AudioContext;
probe = probeAudioDevice();
assert.equal(probe.sampleRate, DEFAULT_SAMPLE_RATE);
assert.match(probe.error ?? '', /not available/);

console.log('deviceProbe tests passed');
