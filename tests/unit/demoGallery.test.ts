import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { isDesktopSavestate } from '../../src/samples/desktopSavestate.ts';
import { parseDemoIndex } from '../../src/ui/demoIndex.ts';

// The bundled index (scripts/gen_demos.py): at least 8 demos, each file present and a WASM patch.
const demos = parseDemoIndex(readFileSync('wasm/resource-pack/demos/index.json', 'utf8'));
assert.ok(demos.length >= 8, `expected >= 8 demos, got ${demos.length}`);
for (const demo of demos) {
  const path = `wasm/resource-pack/demos/${demo.file}`;
  assert.ok(existsSync(path), `${path} is missing`);
  const patch = JSON.parse(readFileSync(path, 'utf8')) as { format?: string; modules?: unknown[] };
  assert.equal(patch.format, 'bespokesynth-wasm-state', `${demo.id} is a WASM patch`);
  assert.ok(demo.title.length > 0 && demo.description.length > 0 && demo.modules.length > 0);
}
assert.equal(new Set(demos.map((demo) => demo.id)).size, demos.length, 'demo ids are unique');

// Malformed input yields no demos, and malformed entries are skipped.
assert.deepEqual(parseDemoIndex(''), []);
assert.deepEqual(parseDemoIndex('not json'), []);
assert.deepEqual(parseDemoIndex('{"demos": [{"id": 1}]}'), []);

// Desktop .bsk detection: a FileStream length prefix, never JSON text.
const desktop = new Uint8Array(readFileSync('resource/userdata_original/savestate/example__sequencing_sequencers.bsk'));
assert.ok(isDesktopSavestate(desktop.subarray(0, 8)));
const wasmPatch = new TextEncoder().encode(readFileSync('resource/userdata_original/savestate/wasm-starter.bsk', 'utf8'));
assert.ok(!isDesktopSavestate(wasmPatch.subarray(0, 8)));
assert.ok(!isDesktopSavestate(new Uint8Array([1, 2, 3])));

console.log('demoGallery tests passed');
