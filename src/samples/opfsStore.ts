const ROOT_DIR = 'bespokesynth-samples';

async function samplesDirectory(): Promise<FileSystemDirectoryHandle> {
  const root = await navigator.storage.getDirectory();
  return root.getDirectoryHandle(ROOT_DIR, { create: true });
}

export async function sha256Hex(data: ArrayBuffer): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', data);
  return Array.from(new Uint8Array(digest))
    .map((byte) => byte.toString(16).padStart(2, '0'))
    .join('');
}

export async function putSampleBlob(hash: string, bytes: ArrayBuffer, name: string): Promise<void> {
  const dir = await samplesDirectory();
  const file = await dir.getFileHandle(hash, { create: true });
  const writable = await file.createWritable();
  await writable.write(bytes);
  await writable.close();
  const meta = await dir.getFileHandle(`${hash}.json`, { create: true });
  const metaWrite = await meta.createWritable();
  await metaWrite.write(JSON.stringify({ name, hash, size: bytes.byteLength }));
  await metaWrite.close();
}

export async function getSampleBlob(hash: string): Promise<{ bytes: ArrayBuffer; name: string } | null> {
  try {
    const dir = await samplesDirectory();
    const file = await dir.getFileHandle(hash);
    const bytes = await (await file.getFile()).arrayBuffer();
    let name = hash.slice(0, 8);
    try {
      const meta = await dir.getFileHandle(`${hash}.json`);
      const parsed = JSON.parse(await (await meta.getFile()).text()) as { name?: string };
      if (parsed.name) name = parsed.name;
    } catch {
      // metadata is optional
    }
    return { bytes, name };
  } catch {
    return null;
  }
}

export function collectSampleHashes(patchJson: string): string[] {
  try {
    const parsed = JSON.parse(patchJson) as {
      modules?: Array<{ extras?: { sampleHash?: string } }>;
    };
    const hashes = new Set<string>();
    for (const module of parsed.modules ?? []) {
      const hash = module.extras?.sampleHash;
      if (hash) hashes.add(hash);
    }
    return [...hashes];
  } catch {
    return [];
  }
}
