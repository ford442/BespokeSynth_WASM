import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { toArrayBuffer } from './bytes';
import { collectSampleHashes, getSampleBlob, putSampleBlob } from './opfsStore';
import { assignSampleToModule, findOrCreateSampler, loadSampleIntoWasm } from './sampleIo';
import { getPatchStateJson, loadPatchStateJson } from '../patchState';

const MAGIC = new TextEncoder().encode('BSPK1\n');

function writeU32(view: DataView, offset: number, value: number): void {
  view.setUint32(offset, value, true);
}

function readU32(view: DataView, offset: number): number {
  return view.getUint32(offset, true);
}

export async function buildBspkBundle(module: BespokeSynthModule): Promise<Uint8Array> {
  const json = getPatchStateJson(module);
  const hashes = collectSampleHashes(json);
  const samples: Array<{ hash: string; name: string; bytes: ArrayBuffer }> = [];
  for (const hash of hashes) {
    const stored = await getSampleBlob(hash);
    if (stored) samples.push({ hash, name: stored.name, bytes: stored.bytes });
  }

  const jsonBytes = new TextEncoder().encode(json);
  let size = MAGIC.length + 4 + jsonBytes.length + 4;
  for (const sample of samples) {
    size += 2 + sample.hash.length + 2 + new TextEncoder().encode(sample.name).length + 4 + sample.bytes.byteLength;
  }

  const out = new Uint8Array(size);
  const view = new DataView(out.buffer);
  let offset = 0;
  out.set(MAGIC, offset);
  offset += MAGIC.length;
  writeU32(view, offset, jsonBytes.length);
  offset += 4;
  out.set(jsonBytes, offset);
  offset += jsonBytes.length;
  writeU32(view, offset, samples.length);
  offset += 4;
  const encoder = new TextEncoder();
  for (const sample of samples) {
    const hashBytes = encoder.encode(sample.hash);
    const nameBytes = encoder.encode(sample.name);
    view.setUint16(offset, hashBytes.length, true);
    offset += 2;
    out.set(hashBytes, offset);
    offset += hashBytes.length;
    view.setUint16(offset, nameBytes.length, true);
    offset += 2;
    out.set(nameBytes, offset);
    offset += nameBytes.length;
    writeU32(view, offset, sample.bytes.byteLength);
    offset += 4;
    out.set(new Uint8Array(sample.bytes), offset);
    offset += sample.bytes.byteLength;
  }
  return out;
}

export async function importBspkBundle(module: BespokeSynthModule, bytes: Uint8Array): Promise<boolean> {
  if (bytes.length < MAGIC.length + 8) return false;
  for (let i = 0; i < MAGIC.length; i++) {
    if (bytes[i] !== MAGIC[i]) return false;
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let offset = MAGIC.length;
  const jsonLen = readU32(view, offset);
  offset += 4;
  const json = new TextDecoder().decode(bytes.subarray(offset, offset + jsonLen));
  offset += jsonLen;
  const count = readU32(view, offset);
  offset += 4;
  const decoder = new TextDecoder();
  for (let i = 0; i < count; i++) {
    const hashLen = view.getUint16(offset, true);
    offset += 2;
    const hash = decoder.decode(bytes.subarray(offset, offset + hashLen));
    offset += hashLen;
    const nameLen = view.getUint16(offset, true);
    offset += 2;
    const name = decoder.decode(bytes.subarray(offset, offset + nameLen));
    offset += nameLen;
    const dataLen = readU32(view, offset);
    offset += 4;
    const data = bytes.subarray(offset, offset + dataLen);
    offset += dataLen;
    await putSampleBlob(hash, data.slice().buffer, name);
    loadSampleIntoWasm(module, data, name);
    const samplerId = findOrCreateSampler(module);
    if (samplerId >= 0) assignSampleToModule(module, samplerId, hash);
  }
  return loadPatchStateJson(module, json);
}

export function downloadBspkBundle(bytes: Uint8Array, filename = 'patch.bspk'): void {
  const blob = new Blob([toArrayBuffer(bytes)], { type: 'application/octet-stream' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  link.click();
  URL.revokeObjectURL(url);
}
