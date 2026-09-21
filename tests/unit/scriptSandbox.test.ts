import assert from 'node:assert/strict';
import {
  BLOCKED_WORKER_GLOBALS,
  MAX_SCRIPT_BYTES,
  WORKER_BOOTSTRAP,
  sanitizeWorkerMessage,
  validateScriptSource,
} from '../../src/script/scriptSandbox.ts';

assert.equal(validateScriptSource('bespoke.log("hi")'), null);
assert.match(validateScriptSource('import("https://example.com/x.js")') ?? '', /import/);
assert.match(validateScriptSource('import x from "y"') ?? '', /import/);
assert.match(validateScriptSource('x'.repeat(MAX_SCRIPT_BYTES + 1)) ?? '', /larger/);

for (const name of ['fetch', 'XMLHttpRequest', 'WebSocket', 'importScripts', 'indexedDB', 'WebAssembly', 'postMessage']) {
  assert.ok(BLOCKED_WORKER_GLOBALS.includes(name), `${name} is scrubbed from the worker`);
  assert.ok(WORKER_BOOTSTRAP.includes(`"${name}"`), `bootstrap embeds ${name}`);
}
assert.ok(!WORKER_BOOTSTRAP.includes('__BLOCKED__'));
assert.doesNotThrow(() => new Function(WORKER_BOOTSTRAP), 'bootstrap is valid JavaScript');

assert.deepEqual(sanitizeWorkerMessage({ type: 'setControl', id: 3, name: 'frequency', value: 440 }), {
  type: 'setControl', id: 3, name: 'frequency', value: 440,
});
assert.equal(sanitizeWorkerMessage({ type: 'setControl', id: 3, name: 'frequency', value: Infinity }), null);
assert.equal(sanitizeWorkerMessage({ type: 'setControl', id: 3, name: '../../evil', value: 1 }), null);
assert.equal(sanitizeWorkerMessage({ type: 'setControl', id: 1.5, name: 'gain', value: 1 }), null);
assert.equal(sanitizeWorkerMessage({ type: 'note', pitch: 200, on: true }), null);
assert.deepEqual(sanitizeWorkerMessage({ type: 'note', pitch: 60, velocity: 5, on: true }), {
  type: 'note', pitch: 60, velocity: 1, on: true,
});
assert.equal(sanitizeWorkerMessage({ type: 'eval', code: 'x' }), null);
assert.equal(sanitizeWorkerMessage(null), null);
const longLog = sanitizeWorkerMessage({ type: 'log', text: 'y'.repeat(10000) });
assert.ok(longLog && longLog.type === 'log' && longLog.text.length === 500);

console.log('scriptSandbox tests passed');
