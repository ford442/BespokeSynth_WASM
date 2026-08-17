import { toArrayBuffer } from './bytes';

function toBase64Url(bytes: Uint8Array): string {
  let binary = '';
  for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '');
}

function fromBase64Url(value: string): Uint8Array {
  const padded = value.replace(/-/g, '+').replace(/_/g, '/').padEnd(Math.ceil(value.length / 4) * 4, '=');
  const binary = atob(padded);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out;
}

async function deflateBytes(bytes: Uint8Array): Promise<Uint8Array> {
  if (typeof CompressionStream === 'undefined') return bytes;
  const stream = new Blob([toArrayBuffer(bytes)]).stream().pipeThrough(new CompressionStream('deflate'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

async function inflateBytes(bytes: Uint8Array): Promise<Uint8Array> {
  if (typeof DecompressionStream === 'undefined') return bytes;
  try {
    const stream = new Blob([toArrayBuffer(bytes)]).stream().pipeThrough(new DecompressionStream('deflate'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  } catch {
    return bytes;
  }
}

export async function encodePatchFragment(json: string): Promise<string> {
  const compressed = await deflateBytes(new TextEncoder().encode(json));
  return `p=${toBase64Url(compressed)}`;
}

export async function decodePatchFragment(hash: string): Promise<string | null> {
  const raw = hash.startsWith('#') ? hash.slice(1) : hash;
  const params = new URLSearchParams(raw);
  const payload = params.get('p');
  if (!payload) return null;
  const inflated = await inflateBytes(fromBase64Url(payload));
  return new TextDecoder().decode(inflated);
}

export async function writeShareUrl(json: string): Promise<string> {
  const fragment = await encodePatchFragment(json);
  const url = new URL(window.location.href);
  url.hash = fragment;
  history.replaceState(null, '', url.toString());
  return url.toString();
}

export function readShareFragment(searchHash = window.location.hash): string {
  return searchHash;
}
