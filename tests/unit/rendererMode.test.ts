import assert from 'node:assert/strict';
import { rendererFooterText, resolveRendererBackend } from '../../src/rendererMode.ts';

// --- Backend selection: WebGPU by default, WebGL2 only on an explicit opt-in ---
assert.equal(resolveRendererBackend(''), 'webgpu', 'no flag: WebGPU');
assert.equal(resolveRendererBackend('?renderer=webgl'), 'webgl');
assert.equal(resolveRendererBackend('?renderer=webgl2'), 'webgl');
assert.equal(resolveRendererBackend('?webgl'), 'webgl');
assert.equal(resolveRendererBackend('?webgl2=1'), 'webgl', '?webgl2=1 opts in');
assert.equal(resolveRendererBackend('?webgl2'), 'webgl', 'bare ?webgl2 opts in');
assert.equal(resolveRendererBackend('?webgl2=0'), 'webgpu', '?webgl2=0 does not opt in');
assert.equal(resolveRendererBackend('?webgl2=false'), 'webgpu');
assert.equal(resolveRendererBackend('?renderer=webgpu'), 'webgpu');

// --- Footer text never claims a backend that is not running ---
assert.match(rendererFooterText('webgpu', null), /WebGPU-accelerated/);
for (const reason of [null, 'WebGPU device lost; switched to WebGL2']) {
  const text = rendererFooterText('webgl', reason);
  assert.doesNotMatch(text, /WebGPU-accelerated/, 'WebGL2 must never read as WebGPU-accelerated');
  assert.match(text, /Renderer: WebGL2/);
}
assert.match(rendererFooterText('webgl', 'x'), /fallback/);
assert.doesNotMatch(rendererFooterText(null, null), /WebGPU-accelerated/);
assert.match(rendererFooterText(null, null), /WebGPU unavailable/);

console.log('rendererMode tests passed');
